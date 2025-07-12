#include <Arduino.h>
#include "battery.h"

// Battery voltage reading configuration
const int BATTERY_PIN = 0; // Battery voltage sense pin

// The following voltage range is for a standard 3.7V LiPo battery.
// A fully charged LiPo is 4.2V (100%), and it is considered fully discharged at 3.0V (0%).
const float VOLTAGE_MIN = 3.0; // Minimum voltage for 0%
const float VOLTAGE_MAX = 4.2; // Maximum voltage for 100%

/**
 * @brief Reads the raw battery voltage.
 *
 * @return The raw voltage in Volts.
 */
float read_battery_voltage() {
  // Set attenuation for the ADC pin to allow reading up to 3.3V
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  // Read the raw voltage in millivolts
  float millivolts = analogReadMilliVolts(BATTERY_PIN);

  // Convert millivolts to volts withvoltage divder
  float voltage = millivolts / 1000.0 * 2;
  return voltage;
}

/**
 * @brief Reads the battery voltage and returns it as a percentage.
 *
 * @return The battery level from 0 to 100.
 */
int read_battery_level() {
  float voltage = read_battery_voltage();

  // Clamp the voltage to the expected range to prevent issues
  if (voltage < VOLTAGE_MIN) {
    voltage = VOLTAGE_MIN;
  }
  if (voltage > VOLTAGE_MAX) {
    voltage = VOLTAGE_MAX;
  }

  // Calculate the percentage based on the clamped voltage
  int percentage = (int)(((voltage - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN)) * 100.0);

  return percentage;
}
