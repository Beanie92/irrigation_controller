#include "Globals.h"

// -----------------------------------------------------------------------------
// GLOBAL CONSTANTS / VARIABLES (definitions)
// -----------------------------------------------------------------------------

// Rotary Encoder Inputs
const int pinA = 4;     // KY-040 CLK
const int pinB = 7;     // KY-040 DT
const int button = 16;  // KY-040 SW (with internal pull-up)

const unsigned long buttonDebounce = 200;  // milliseconds

// Relay Pins / Configuration
const int NUM_RELAYS = 8;
const int relayPins[NUM_RELAYS] = { 19, 20, 9, 18, 15, 21, 1, 14 };
bool relayStates[NUM_RELAYS] = { false, false, false, false, false, false, false, false };

const int PUMP_IDX = 0;
const int ZONE_COUNT = 7;

// Display Pins
const int TFT_DC  = 2;
const int TFT_CS  = 6;
const int TFT_RST = 3;
DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

// Program State
ProgramState currentState = STATE_MAIN_MENU;

// Menu items
const int MAIN_MENU_ITEMS = 6;
const char* mainMenuLabels[MAIN_MENU_ITEMS] = {
  "Manual Run",
  "Set System Time",
  "Set Cycle Start",
  "Program A",
  "Program B",
  "Program C"
};
int selectedMainMenuIndex = 0;

// Manual Run zone
int selectedManualZoneIndex = 0;

// Current software time
SystemDateTime currentDateTime = { 2025, 2, 6, 19, 47, 0 };
unsigned long lastSecondUpdate = 0;

// Program configurations
ProgramConfig programA = { { 5, 5, 5, 5, 5, 5, 5 }, 1 };
ProgramConfig programB = { { 10, 10, 10, 10, 10, 10, 10 }, 2 };
ProgramConfig programC = { { 3, 3, 3, 3, 3, 3, 3 }, 0 };

// Cycle start time
SystemDateTime cycleStartTime = { 2023, 1, 1, 6, 0, 0 }; // e.g. 06:00 daily

// Preferences
Preferences preferences;
