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
 * @brief Reads the current from the WCS1800 sensor with averaging.
 * 
 * @return The current in Amperes.
 */
float read_wcs1800_current() {
  const int NUM_SAMPLES = 100;
  float total_voltage_mv = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    total_voltage_mv += analogReadMilliVolts(WCS1800_PIN);
    delay(1); // Small delay for stability
  }

  float average_voltage_mv = total_voltage_mv / NUM_SAMPLES;
  
  // Convert measured millivolts to volts
  float voltage = average_voltage_mv / 1000.0;

  // Calculate the current in Amperes using the provided calibration values
  // Formula: Current = (MeasuredVoltage - VoltageAtZeroCurrent) / Sensitivity
  float current = (voltage - VREF_ZERO_CURRENT) / SENSITIVITY;

  return current;
}

// --- Current History ---
const float COV_THRESHOLD = 0.2f; // 200 mA
const uint64_t MIN_TIME_INTERVAL_US = 900000ULL * 1000; // 15 minutes in microseconds
const size_t MAX_HISTORY_SIZE = 200;
const uint64_t MIN_SAMPLE_INTERVAL_US = 500ULL * 1000; // Minimum time between samples in microseconds

static std::vector<CurrentHistoryEntry> current_history;
static uint64_t last_update_time_us = 0;
static float last_recorded_current = 0.0f;

void update_current_history() {
    static uint64_t last_sample_time_us = 0;
    uint64_t current_time_us = esp_timer_get_time();

    // Enforce a minimum delay between samples
    if (current_time_us - last_sample_time_us < MIN_SAMPLE_INTERVAL_US) {
        return;
    }
    last_sample_time_us = current_time_us;

    float current_now = read_wcs1800_current();

    bool cov_triggered = abs(current_now - last_recorded_current) > COV_THRESHOLD;
    bool time_triggered = (current_time_us - last_update_time_us) > MIN_TIME_INTERVAL_US;

    if (cov_triggered || time_triggered) {
        if (current_history.size() >= MAX_HISTORY_SIZE) {
            current_history.erase(current_history.begin());
        }
        // Store timestamp in milliseconds for downstream processes
        current_history.push_back({current_time_us / 1000, current_now});
        last_update_time_us = current_time_us;
        last_recorded_current = current_now;
    }
}

const std::vector<CurrentHistoryEntry>& get_current_history() {
    return current_history;
}
