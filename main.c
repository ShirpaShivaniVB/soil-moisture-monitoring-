/*
 * Soil Moisture Monitoring System
 * Hardware: ESP32
 * Sensor: Capacitive Soil Moisture Sensor (analog output)
 *
 * Features:
 *   - Reads soil moisture via ADC
 *   - Classifies into Dry / Moist / Wet states
 *   - Controls irrigation relay based on threshold
 *   - Logs readings over Serial
 *   - Publishes status to MQTT topic
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc_cal.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "config.h"

static const char *TAG = "SOIL_MONITOR";

/* ADC calibration handle */
static esp_adc_cal_characteristics_t adc_chars;

/* MQTT */
static esp_mqtt_client_handle_t mqtt_client;
static bool mqtt_ready = false;

/* ─────────────────────────────────────────────
 *  Moisture Classification
 * ───────────────────────────────────────────── */
typedef enum {
    MOISTURE_DRY  = 0,
    MOISTURE_MOIST = 1,
    MOISTURE_WET  = 2,
} moisture_class_t;

static const char *class_labels[] = { "DRY", "MOIST", "WET" };

static moisture_class_t classify_moisture(uint32_t voltage_mv)
{
    /*
     * Capacitive sensor output (inverted — lower voltage = wetter):
     *   Dry soil:   voltage > DRY_THRESHOLD_MV
     *   Moist soil: WET_THRESHOLD_MV < voltage <= DRY_THRESHOLD_MV
     *   Wet soil:   voltage <= WET_THRESHOLD_MV
     */
    if (voltage_mv > DRY_THRESHOLD_MV)  return MOISTURE_DRY;
    if (voltage_mv > WET_THRESHOLD_MV)  return MOISTURE_MOIST;
    return MOISTURE_WET;
}

/* ─────────────────────────────────────────────
 *  Irrigation Control
 *  Relay is active-HIGH (HIGH = pump ON)
 * ───────────────────────────────────────────── */
static bool irrigation_active = false;

static void set_irrigation(bool enable)
{
    if (enable == irrigation_active) return;   /* No change */

    gpio_set_level(RELAY_GPIO, enable ? 1 : 0);
    irrigation_active = enable;
    ESP_LOGI(TAG, "Irrigation %s", enable ? "ON" : "OFF");
}

static void update_irrigation(moisture_class_t class)
{
    switch (class) {
        case MOISTURE_DRY:
            set_irrigation(true);   /* Soil dry → start irrigation */
            break;
        case MOISTURE_MOIST:
            /* Keep current state — avoid hunting */
            break;
        case MOISTURE_WET:
            set_irrigation(false);  /* Soil wet → stop irrigation */
            break;
    }
}

/* ─────────────────────────────────────────────
 *  ADC Read (calibrated, averaged)
 * ───────────────────────────────────────────── */
static uint32_t read_moisture_voltage(void)
{
    uint32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        adc_sum += adc1_get_raw(MOISTURE_ADC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    uint32_t avg_raw = adc_sum / ADC_SAMPLES;

    /* Convert raw ADC to millivolts using ESP32 calibration */
    return esp_adc_cal_raw_to_voltage(avg_raw, &adc_chars);
}

/* ─────────────────────────────────────────────
 *  MQTT Helpers
 * ───────────────────────────────────────────── */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *event_data)
{
    esp_mqtt_event_handle_t e = event_data;
    if (e->event_id == MQTT_EVENT_CONNECTED) {
        mqtt_ready = true;
        ESP_LOGI(TAG, "MQTT connected");
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = { .broker.address.uri = MQTT_BROKER_URI };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void publish_status(uint32_t voltage_mv, moisture_class_t class, bool irrigating)
{
    if (!mqtt_ready) return;
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"voltage_mv\":%lu,\"class\":\"%s\",\"irrigation\":%s}",
             voltage_mv, class_labels[class], irrigating ? "true" : "false");
    esp_mqtt_client_publish(mqtt_client, MQTT_STATUS_TOPIC, payload, 0, 0, 0);
}

/* ─────────────────────────────────────────────
 *  WiFi
 * ───────────────────────────────────────────── */
static SemaphoreHandle_t wifi_sem;
static void wifi_handler(void *a, esp_event_base_t b, int32_t id, void *d)
{
    if (b == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xSemaphoreGive(wifi_sem);
    else if (b == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
}

static void wifi_init(void)
{
    wifi_sem = xSemaphoreCreateBinary();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL);
    wifi_config_t wc = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD } };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    xSemaphoreTake(wifi_sem, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
}

/* ─────────────────────────────────────────────
 *  Main Monitoring Task
 * ───────────────────────────────────────────── */
static void soil_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Soil monitor task started");
    ESP_LOGI(TAG, "Thresholds — Dry: >%d mV | Wet: <%d mV",
             DRY_THRESHOLD_MV, WET_THRESHOLD_MV);

    while (1) {
        uint32_t voltage_mv   = read_moisture_voltage();
        moisture_class_t class = classify_moisture(voltage_mv);

        ESP_LOGI(TAG, "Voltage: %lu mV | Class: %s | Irrigation: %s",
                 voltage_mv, class_labels[class],
                 irrigation_active ? "ON" : "OFF");

        update_irrigation(class);
        publish_status(voltage_mv, class, irrigation_active);

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
    }
}

/* ─────────────────────────────────────────────
 *  app_main
 * ───────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Soil Moisture Monitoring System ===");

    ESP_ERROR_CHECK(nvs_flash_init());

    /* Configure ADC */
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(MOISTURE_ADC_CHANNEL, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                             ADC_WIDTH_BIT_12, 1100, &adc_chars);

    /* Configure relay GPIO */
    gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_GPIO, 0);   /* Irrigation OFF at startup */

    wifi_init();
    mqtt_init();

    xTaskCreate(soil_monitor_task, "soil_monitor", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "System running.");
}
