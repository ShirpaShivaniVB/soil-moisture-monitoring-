# Soil Moisture Monitoring System

A real-time soil moisture monitoring system built on **ESP32** using **Embedded C**. Reads a capacitive soil moisture sensor, classifies soil condition (Dry / Moist / Wet), automatically controls an irrigation relay, and publishes status to an MQTT broker.

---

## Features

- Calibrated ADC reading (16-sample averaged) using ESP32 ADC calibration API
- Threshold-based classification: **Dry / Moist / Wet**
- Automatic irrigation relay control — turns pump ON when dry, OFF when wet
- Hysteresis in the Moist state prevents relay hunting
- Real-time MQTT status publishing (voltage, class, irrigation state)
- Fully configurable thresholds in `config.h`

---

## Hardware Required

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 DevKit V1 |
| Sensor | Capacitive soil moisture sensor (3.3V compatible) |
| Relay module | 5V relay module (active HIGH) |
| Pump | 5V mini submersible pump (optional) |

### Wiring

```
Moisture Sensor AOUT  →  GPIO34 (ADC1 CH6)
Moisture Sensor VCC   →  3.3V
Moisture Sensor GND   →  GND

Relay IN              →  GPIO26
Relay VCC             →  5V (external supply)
Relay GND             →  GND
```

> **Note:** Use a capacitive sensor (not resistive). Resistive sensors corrode quickly in soil.

---

## Sensor Calibration

The sensor output is **inverted** — lower voltage means wetter soil.

```bash
# After flashing, open serial monitor
idf.py -p /dev/ttyUSB0 monitor

# 1. Hold sensor in dry air → note voltage → set DRY_THRESHOLD_MV slightly below
# 2. Submerge sensor in water → note voltage → set WET_THRESHOLD_MV slightly above
# 3. Update config.h and reflash
```

---

## Setup

```bash
git clone https://github.com/ShirpaShivaniVB/soil-moisture-monitoring
cd soil-moisture-monitoring

nano config.h   # Set WiFi credentials and moisture thresholds

idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## MQTT Status Payload

Published every 5 seconds to `soil_monitor/status`:

```json
{
  "voltage_mv": 2150,
  "class": "MOIST",
  "irrigation": false
}
```

---

## Classification Logic

| Voltage (mV) | Class | Irrigation |
|---|---|---|
| > 2800 | DRY | ON |
| 1500 – 2800 | MOIST | No change (hysteresis) |
| < 1500 | WET | OFF |

---

## Skills Demonstrated

`Embedded C` `ESP32` `ADC` `GPIO` `Relay Control` `IoT` `MQTT` `FreeRTOS` `Sensor Calibration`
