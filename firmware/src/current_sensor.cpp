#include <Arduino.h>
#include "current_sensor.h"
#include <vector>
#include <cstdint>
#include <cmath>

// WCS1800 Current Sensor Configuration for ESP32
const int WCS1800_PIN = 1;       // WCS1800 connected to ESP32 analog pin 1 (ADC1_CH0)

// Using user-provided calibration values
const float SENSITIVITY = 0.0101;   // Sensitivity in V/A from calibration
const float VREF_ZERO_CURRENT = 1.632; // Reference voltage at zero current from calibration (in Volts)

/**
 * @brief Initializes the current sensor pin.
 * On the ESP32, setting the attenuation is good practice for analog inputs.
 * ADC_11db gives a full-range of approx. 0-3.3V.
 */
void setup_current_sensor() {
  analogSetPinAttenuation(WCS1800_PIN, ADC_11db);
}

/**
 * @brief Reads the current from the WCS1800 sensor.
 * 
 * @return The current in Amperes.
 */
float read_wcs1800_current() {
  // Read the voltage from the sensor in millivolts
  float voltage_mv = analogReadMilliVolts(WCS1800_PIN);
  
  // Convert measured millivolts to volts
  float voltage = voltage_mv / 1000.0;

  // Calculate the current in Amperes using the provided calibration values
  // Formula: Current = (MeasuredVoltage - VoltageAtZeroCurrent) / Sensitivity
  float current = (voltage - VREF_ZERO_CURRENT) / SENSITIVITY;

  return current;
}

// --- Current History ---
const float COV_THRESHOLD = 0.2f; // 200 mA
const uint32_t MIN_TIME_INTERVAL_MS = 900000; // 15 minutes
const size_t MAX_HISTORY_SIZE = 200;
const uint32_t MIN_SAMPLE_INTERVAL_MS = 500; // Minimum time between samples

static std::vector<CurrentHistoryEntry> current_history;
static uint32_t last_update_time = 0;
static float last_recorded_current = 0.0f;

void update_current_history() {
    static uint32_t last_sample_time = 0;
    uint32_t current_time = millis();

    // Enforce a minimum delay between samples
    if (current_time - last_sample_time < MIN_SAMPLE_INTERVAL_MS) {
        return;
    }
    last_sample_time = current_time;

    float current_now = read_wcs1800_current();

    if (isnan(current_now)) {
        return; // Do not record NaN values
    }

    bool cov_triggered = abs(current_now - last_recorded_current) > COV_THRESHOLD;
    bool time_triggered = (current_time - last_update_time) > MIN_TIME_INTERVAL_MS;

    if (cov_triggered || time_triggered) {
        if (current_history.size() >= MAX_HISTORY_SIZE) {
            current_history.erase(current_history.begin());
        }
        current_history.push_back({current_time, current_now});
        last_update_time = current_time;
        last_recorded_current = current_now;
    }
}

const std::vector<CurrentHistoryEntry>& get_current_history() {
    return current_history;
}
