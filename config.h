#pragma once
#include "driver/adc.h"
#include "driver/gpio.h"

/* ── WiFi ───────────────────────────────────── */
#define WIFI_SSID              "your_wifi_ssid"
#define WIFI_PASSWORD          "your_wifi_password"

/* ── MQTT ───────────────────────────────────── */
#define MQTT_BROKER_URI        "mqtt://broker.hivemq.com"
#define MQTT_STATUS_TOPIC      "soil_monitor/status"

/* ── Sensor ADC ─────────────────────────────── */
#define MOISTURE_ADC_CHANNEL   ADC1_CHANNEL_6   /* GPIO34 */
#define ADC_SAMPLES            16               /* Average 16 samples per read */

/* ── Moisture Thresholds (calibrate for your sensor) ── */
/*
 * Capacitive sensor output is INVERTED:
 *   High voltage = dry soil
 *   Low voltage  = wet soil
 *
 * To calibrate:
 *   1. Read voltage in completely dry soil → set DRY_THRESHOLD_MV slightly below
 *   2. Read voltage in water → set WET_THRESHOLD_MV slightly above
 */
#define DRY_THRESHOLD_MV       2800   /* Above this = DRY */
#define WET_THRESHOLD_MV       1500   /* Below this = WET */

/* ── GPIO ───────────────────────────────────── */
#define RELAY_GPIO             GPIO_NUM_26  /* Controls irrigation pump relay */

/* ── Timing ─────────────────────────────────── */
#define READ_INTERVAL_MS       5000    /* Check moisture every 5 seconds */
