#include "Globals.h"
#include "EncoderManager.h"
#include "MenuManager.h"
#include "TimeManager.h"
#include "NVSManager.h"

/*
  This is the main entry point for the Arduino/ESP32 application.
  setup() and loop() remain essentially the same as in your
  original monolithic code, but they delegate functionality
  to the various modules.
*/

void setup() {
  Serial.begin(115200);
  Serial.println("Extended Menu Example w/ Non-Volatile Storage (ESP32).");

  // Initialize Preferences (NVS)
  preferences.begin("myIrrigation", false);
  loadAllFromNVS();   // Load stored config if available
  preferences.end();  // We'll reopen it whenever we need to save

  // Initialize display
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Rotary encoder pins
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
  pinMode(button, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinA), isrPinA, CHANGE);

  // Relay pins
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
    relayStates[i] = false;
  }

  // Go to main menu initially
  enterState(STATE_MAIN_MENU);
}

void loop() {
  // Keep the software clock updated
  updateSoftwareClock();

  // Check encoder turning
  handleEncoderMovement();

  // Check button
  handleButtonPress();

  // If running a zone, your logic might check for timeouts, etc.
}
