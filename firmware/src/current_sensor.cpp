#include <Arduino.h>
#include "current_sensor.h"

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
