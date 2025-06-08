
#include <Arduino.h>
#include "DFRobot_GDL.h"
#include "logo.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <Preferences.h>

// -----------------------------------------------------------------------------
//                           DEBUG CONFIGURATION
// -----------------------------------------------------------------------------
#define DEBUG_ENABLED true  // Set to false to disable all debug logging

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(format, ...)
#endif

// -----------------------------------------------------------------------------
//                    Rotary Encoder Inputs / Global Variables
// -----------------------------------------------------------------------------
static const int pinA   = 4;   // KY-040 CLK
static const int pinB   = 7;   // KY-040 DT
static const int button = 16;  // KY-040 SW (with internal pull-up)

volatile long encoderValue = 0;
volatile bool encoderMoved = false;

// For button software debounce:
unsigned long lastButtonPressTime = 0;
const unsigned long buttonDebounce = 200; // milliseconds

// -----------------------------------------------------------------------------
//                        Relay Pins / Configuration
// -----------------------------------------------------------------------------
static const int NUM_RELAYS = 8;
// Relay 0 is dedicated to the borehole pump;
// Relays 1..7 are the irrigation zones.
static const int relayPins[NUM_RELAYS] = {19, 20, 17, 18, 15, 21, 1, 14}; 
bool relayStates[NUM_RELAYS] = {false, false, false, false, false, false, false, false};

static const int PUMP_IDX = 0;   // borehole pump
static const int ZONE_COUNT = 7; // zones 1..7

// -----------------------------------------------------------------------------
//                           Display Pins / Driver
// -----------------------------------------------------------------------------
#define TFT_DC   2
#define TFT_CS   6
#define TFT_RST  3

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

// Relay labels (index 0 is the pump)
const char* relayLabels[NUM_RELAYS] = {
  "Pump (auto)", // index 0; not displayed in manual-run menu
  "Zone 1",
  "Zone 2",
  "Zone 3",
  "Zone 4",
  "Zone 5",
  "Zone 6",
  "Zone 7"
};

// -----------------------------------------------------------------------------
//                           Menu and Program States
// -----------------------------------------------------------------------------
enum ProgramState {
  STATE_MAIN_MENU,
  STATE_MANUAL_RUN,        
  STATE_SETTINGS,
  STATE_SET_SYSTEM_TIME,
  STATE_WIFI_SETUP,
  STATE_WIFI_RESET,
  STATE_SYSTEM_INFO,
  STATE_SET_CYCLE_START,
  STATE_PROG_A,
  STATE_PROG_B,
  STATE_PROG_C,
  STATE_RUNNING_ZONE,
  STATE_TEST_MODE
};

ProgramState currentState = STATE_MAIN_MENU;

// Main Menu Items
static const int MAIN_MENU_ITEMS = 7;
const char* mainMenuLabels[MAIN_MENU_ITEMS] = {
  "Manual Run",
  "Test Mode",
  "Settings",
  "Set Cycle Start",
  "Program A",
  "Program B",
  "Program C"
};
int selectedMainMenuIndex = 0; 

// Settings Menu Items
static const int SETTINGS_MENU_ITEMS = 5;
const char* settingsMenuLabels[SETTINGS_MENU_ITEMS] = {
  "WiFi Setup",
  "Set Time Manually",
  "WiFi Reset",
  "System Info",
  "Back to Main Menu"
};
int selectedSettingsMenuIndex = 0;

// Manual Run zone index: 0..6 => zone = index+1
int selectedManualZoneIndex = 0;

// -----------------------------------------------------------------------------
//                           Test Mode Variables
// -----------------------------------------------------------------------------
bool testModeActive = false;
int currentTestRelay = 0;           // 0-7: 0=pump, 1-7=zones
unsigned long testModeStartTime = 0;
const unsigned long TEST_INTERVAL = 5000; // 5 seconds per relay

// -----------------------------------------------------------------------------
//                  Time-Keeping (Software Simulation)
// -----------------------------------------------------------------------------
struct SystemDateTime {
  int year;   // e.g., 2023
  int month;  // 1..12
  int day;    // 1..31
  int hour;   // 0..23
  int minute; // 0..59
  int second; // 0..59
};

SystemDateTime currentDateTime = {2023, 1, 1, 8, 0, 0}; // Example start date/time
unsigned long lastSecondUpdate = 0; // track millis() to increment seconds

void incrementOneSecond() {
  // Very simplistic approach: just add 1 second, then handle minute/hour/day wrap
  currentDateTime.second++;
  if (currentDateTime.second >= 60) {
    currentDateTime.second = 0;
    currentDateTime.minute++;
    if (currentDateTime.minute >= 60) {
      currentDateTime.minute = 0;
      currentDateTime.hour++;
      if (currentDateTime.hour >= 24) {
        currentDateTime.hour = 0;
        currentDateTime.day++;
        // Very naive day wrap (assumes 30 days). In real code, handle months properly.
        if (currentDateTime.day > 30) {
          currentDateTime.day = 1;
          currentDateTime.month++;
          if (currentDateTime.month > 12) {
            currentDateTime.month = 1;
            currentDateTime.year++;
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
//                 Cycle Start Time & Program Config
// -----------------------------------------------------------------------------
typedef enum {
    SUNDAY    = 0b00000001,
    MONDAY    = 0b00000010,
    TUESDAY   = 0b00000100,
    WEDNESDAY = 0b00001000,
    THURSDAY  = 0b00010000,
    FRIDAY    = 0b00100000,
    SATURDAY  = 0b01000000,
    EVERYDAY  = 0b01111111  // All days
} DayOfWeek;

// Time structure (could use instead of SystemDateTime for start time)
typedef struct {
    uint8_t hour;    // 0-23
    uint8_t minute;  // 0-59
} TimeOfDay;

// Main program configuration structure
struct ProgramConfig {
    bool enabled;           // Whether this program is active
    TimeOfDay startTime;    // When to start the program
    uint8_t daysActive;     // Bitfield using DayOfWeek values
    uint8_t interZoneDelay; // Minutes to wait between zones
    uint16_t zoneDurations[ZONE_COUNT]; // Minutes per zone (renamed from zoneRunTimes)
    char name[16];          // Optional: Program name/description
};

ProgramConfig programA = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program A"
};
ProgramConfig programB = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program B"
};
ProgramConfig programC = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program C"
};

// Single "cycleStartTime" for demonstration
SystemDateTime cycleStartTime = {2023,1,1,6,0,0}; // e.g. run at 06:00 each day

// -----------------------------------------------------------------------------
//                Sub-indexes and helpers for editing fields
// -----------------------------------------------------------------------------
static int timeEditFieldIndex = 0;    // 0=year,1=month,2=day,3=hour,4=minute,5=second
static int cycleEditFieldIndex = 0;   // 0=hour,1=minute
static int programEditZoneIndex = 0;  // 0..7 => 0..6=zone durations, 7=interZoneDelay

// Ranges for system time fields (simple example)
#define MIN_YEAR  2020
#define MAX_YEAR  2050

// -----------------------------------------------------------------------------
//                           WiFi and NTP Configuration
// -----------------------------------------------------------------------------
Preferences preferences;
bool wifiConnected = false;
bool timeSync = false;
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000; // Sync every hour (in milliseconds)

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;     // GMT+2 for Botswana (adjust as needed)
const int daylightOffset_sec = 0;    // No daylight saving in Botswana

// WiFi credentials storage keys
const char* WIFI_SSID_KEY = "wifi_ssid";
const char* WIFI_PASS_KEY = "wifi_pass";

// -----------------------------------------------------------------------------
//                           Forward Declarations
// -----------------------------------------------------------------------------
void isrPinA();
void handleEncoderMovement();
void handleButtonPress();

void drawLogo();
void drawMainMenu();
void drawDateTime(int x, int y);
void enterState(ProgramState newState);

void updateSoftwareClock();

// Manual Run
void drawManualRunMenu();
void drawRunningZoneMenu();
void startManualZone(int zoneIdx);
void stopZone();

// Set System Time
void drawSetSystemTimeMenu();
void handleSetSystemTimeEncoder(long diff);
void handleSetSystemTimeButton();

// Set Cycle Start
void drawSetCycleStartMenu();
void handleSetCycleStartEncoder(long diff);
void handleSetCycleStartButton();

// Program A/B/C
void drawProgramConfigMenu(const char* label, ProgramConfig& cfg);
void handleProgramEditEncoder(long diff, ProgramConfig &cfg, const char* progLabel);
void handleProgramEditButton(ProgramConfig &cfg, ProgramState thisState, const char* progLabel);

// Settings menu functions
void drawSettingsMenu();
void drawWiFiSetupMenu();
void drawWiFiResetMenu();
void drawSystemInfoMenu();

// Test Mode functions
void startTestMode();
void updateTestMode();
void drawTestModeMenu();
void stopTestMode();

// WiFi and NTP functions
void initWiFi();
void connectToWiFi();
void syncTimeWithNTP();
void updateTimeFromNTP();
void updateSystemTimeFromNTP();
void resetWiFiCredentials();
void startWiFiSetup();

// -----------------------------------------------------------------------------
//                                     SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("=== IRRIGATION CONTROLLER STARTUP ===");
  DEBUG_PRINTF("Firmware Version: v1.0\n");
  DEBUG_PRINTF("Hardware: ESP32-C6\n");
  DEBUG_PRINTF("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Initialize display
  DEBUG_PRINTLN("Initializing display...");
  screen.begin();
  screen.setRotation(3); // Rotate display 90 degrees clockwise
  screen.fillScreen(COLOR_RGB565_BLACK);
  DEBUG_PRINTLN("Display initialized successfully");

  // Show logo for 3 seconds
  DEBUG_PRINTLN("Displaying logo...");
  drawLogo();
  delay(3000);

  // Rotary encoder pins
  DEBUG_PRINTLN("Configuring rotary encoder pins...");
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
  pinMode(button, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinA), isrPinA, CHANGE);
  DEBUG_PRINTF("Encoder pins configured: CLK=%d, DT=%d, SW=%d\n", pinA, pinB, button);

  // Relay pins
  DEBUG_PRINTLN("Initializing relay pins...");
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
    relayStates[i] = false;
    DEBUG_PRINTF("Relay %d (pin %d): OFF\n", i, relayPins[i]);
  }

  // Initialize preferences for WiFi storage
  DEBUG_PRINTLN("Initializing preferences...");
  preferences.begin("irrigation", false);

  // Start in main menu (WiFi setup is now optional via Settings menu)
  DEBUG_PRINTLN("Entering main menu state...");
  enterState(STATE_MAIN_MENU);
  DEBUG_PRINTLN("=== STARTUP COMPLETE ===");
}

// -----------------------------------------------------------------------------
//                                     LOOP
// -----------------------------------------------------------------------------
void loop() {
  // Update time - use NTP if available, otherwise software clock
  if (timeSync) {
    updateTimeFromNTP(); // Check for periodic NTP sync
    updateSystemTimeFromNTP(); // Update our time structure from system time
  } else {
    updateSoftwareClock(); // Fallback to software clock
  }
  
  handleEncoderMovement();
  handleButtonPress();

  // Additional logic for running states if needed
  if (currentState == STATE_RUNNING_ZONE) {
    // e.g., check time-based zone run or cancellation
  } else if (currentState == STATE_TEST_MODE && testModeActive) {
    updateTestMode();
  }
}

// -----------------------------------------------------------------------------
//                        INTERRUPT SERVICE ROUTINE
// -----------------------------------------------------------------------------
void IRAM_ATTR isrPinA() {
  bool A = digitalRead(pinA);
  bool B = digitalRead(pinB);
  if (A == B) {
    encoderValue = encoderValue - 1;
  } else {
    encoderValue = encoderValue + 1;
  }
  encoderMoved = true;
}

// -----------------------------------------------------------------------------
//                         ENCODER & BUTTON HANDLERS
// -----------------------------------------------------------------------------
static long lastEncoderPosition = 0;
void handleEncoderMovement() {
  if (!encoderMoved) return;

  noInterrupts();
  long newVal = encoderValue;
  encoderMoved = false;
  interrupts();

  long diff = newVal - lastEncoderPosition;
  lastEncoderPosition = newVal;
  if (diff == 0) return;

  DEBUG_PRINTF("Encoder moved: diff=%ld, state=%d\n", diff, currentState);

  switch (currentState) {
    case STATE_MAIN_MENU:
      // Slide through the 6 items
      if (diff > 0) selectedMainMenuIndex++;
      else          selectedMainMenuIndex--;
      if      (selectedMainMenuIndex < 0)               selectedMainMenuIndex = MAIN_MENU_ITEMS - 1;
      else if (selectedMainMenuIndex >= MAIN_MENU_ITEMS) selectedMainMenuIndex = 0;
      DEBUG_PRINTF("Main menu selection: %d (%s)\n", selectedMainMenuIndex, mainMenuLabels[selectedMainMenuIndex]);
      drawMainMenu();
      break;

    case STATE_MANUAL_RUN:
      // Slide through zones 1..7
      if (diff > 0) selectedManualZoneIndex++;
      else          selectedManualZoneIndex--;
      if      (selectedManualZoneIndex < 0)           selectedManualZoneIndex = ZONE_COUNT - 1;
      else if (selectedManualZoneIndex >= ZONE_COUNT) selectedManualZoneIndex = 0;
      DEBUG_PRINTF("Manual run zone selection: %d (%s)\n", selectedManualZoneIndex, relayLabels[selectedManualZoneIndex + 1]);
      drawManualRunMenu();
      break;

    case STATE_SETTINGS:
      // Slide through settings menu items
      if (diff > 0) selectedSettingsMenuIndex++;
      else          selectedSettingsMenuIndex--;
      if      (selectedSettingsMenuIndex < 0)                    selectedSettingsMenuIndex = SETTINGS_MENU_ITEMS - 1;
      else if (selectedSettingsMenuIndex >= SETTINGS_MENU_ITEMS) selectedSettingsMenuIndex = 0;
      DEBUG_PRINTF("Settings menu selection: %d (%s)\n", selectedSettingsMenuIndex, settingsMenuLabels[selectedSettingsMenuIndex]);
      drawSettingsMenu();
      break;

    case STATE_SET_SYSTEM_TIME:
      // Delegate to sub-function that changes the current field
      handleSetSystemTimeEncoder(diff);
      break;

    case STATE_SET_CYCLE_START:
      handleSetCycleStartEncoder(diff);
      break;

    case STATE_PROG_A:
      handleProgramEditEncoder(diff, programA, "Program A");
      break;

    case STATE_PROG_B:
      handleProgramEditEncoder(diff, programB, "Program B");
      break;

    case STATE_PROG_C:
      handleProgramEditEncoder(diff, programC, "Program C");
      break;

    case STATE_RUNNING_ZONE:
      DEBUG_PRINTLN("Encoder ignored - zone is running");
      break;

    default:
      DEBUG_PRINTF("Encoder movement in unknown state: %d\n", currentState);
      break;
  }
}

void handleButtonPress() {
  static bool lastButtonState = HIGH;
  bool currentReading = digitalRead(button);

  if (currentReading == LOW && lastButtonState == HIGH) {
    unsigned long now = millis();
    if ((now - lastButtonPressTime) > buttonDebounce) {
      lastButtonPressTime = now;
      DEBUG_PRINTF("Button pressed in state %d\n", currentState);

      // State-Specific Handling
      switch (currentState) {
        case STATE_MAIN_MENU:
          // User selected an item. Jump to that state:
          DEBUG_PRINTF("Main menu item selected: %d (%s)\n", selectedMainMenuIndex, mainMenuLabels[selectedMainMenuIndex]);
          switch (selectedMainMenuIndex) {
            case 0: // Manual Run
              enterState(STATE_MANUAL_RUN);
              break;
            case 1: // Test Mode
              enterState(STATE_TEST_MODE);
              break;
            case 2: // Settings
              enterState(STATE_SETTINGS);
              break;
            case 3: // Set Cycle Start
              enterState(STATE_SET_CYCLE_START);
              break;
            case 4: // Program A
              enterState(STATE_PROG_A);
              break;
            case 5: // Program B
              enterState(STATE_PROG_B);
              break;
            case 6: // Program C
              enterState(STATE_PROG_C);
              break;
          }
          break;

        case STATE_SETTINGS:
          // User selected a settings item
          DEBUG_PRINTF("Settings menu item selected: %d (%s)\n", selectedSettingsMenuIndex, settingsMenuLabels[selectedSettingsMenuIndex]);
          switch (selectedSettingsMenuIndex) {
            case 0: // WiFi Setup
              enterState(STATE_WIFI_SETUP);
              break;
            case 1: // Set Time Manually
              enterState(STATE_SET_SYSTEM_TIME);
              break;
            case 2: // WiFi Reset
              enterState(STATE_WIFI_RESET);
              break;
            case 3: // System Info
              enterState(STATE_SYSTEM_INFO);
              break;
            case 4: // Back to Main Menu
              enterState(STATE_MAIN_MENU);
              break;
          }
          break;

        case STATE_MANUAL_RUN:
          // Pressing button => Start the selected zone
          DEBUG_PRINTF("Starting manual zone: %d\n", selectedManualZoneIndex + 1);
          startManualZone(selectedManualZoneIndex + 1); // zoneIdx 1..7
          break;

        case STATE_SET_SYSTEM_TIME:
          DEBUG_PRINTF("System time button - field %d\n", timeEditFieldIndex);
          handleSetSystemTimeButton();
          break;

        case STATE_SET_CYCLE_START:
          DEBUG_PRINTF("Cycle start button - field %d\n", cycleEditFieldIndex);
          handleSetCycleStartButton();
          break;

        case STATE_PROG_A:
          DEBUG_PRINTF("Program A button - zone %d\n", programEditZoneIndex);
          handleProgramEditButton(programA, STATE_PROG_A, "Program A");
          break;

        case STATE_PROG_B:
          DEBUG_PRINTF("Program B button - zone %d\n", programEditZoneIndex);
          handleProgramEditButton(programB, STATE_PROG_B, "Program B");
          break;

        case STATE_PROG_C:
          DEBUG_PRINTF("Program C button - zone %d\n", programEditZoneIndex);
          handleProgramEditButton(programC, STATE_PROG_C, "Program C");
          break;

        case STATE_RUNNING_ZONE:
          // Pressing button => Cancel the running zone
          DEBUG_PRINTLN("Cancelling running zone");
          stopZone();
          enterState(STATE_MAIN_MENU);
          break;

        case STATE_TEST_MODE:
          // Pressing button => Cancel test mode
          DEBUG_PRINTLN("Cancelling test mode");
          stopTestMode();
          enterState(STATE_MAIN_MENU);
          break;

        default:
          DEBUG_PRINTF("Button press in unknown state: %d\n", currentState);
          break;
      }
    } else {
      DEBUG_PRINTLN("Button press ignored - debounce");
    }
  }
  lastButtonState = currentReading;
}

// -----------------------------------------------------------------------------
//                            STATE TRANSITIONS
// -----------------------------------------------------------------------------
void enterState(ProgramState newState) {
  const char* stateNames[] = {
    "MAIN_MENU", "MANUAL_RUN", "SET_SYSTEM_TIME", "SET_CYCLE_START",
    "PROG_A", "PROG_B", "PROG_C", "RUNNING_ZONE"
  };
  
  DEBUG_PRINTF("State transition: %s -> %s\n", 
    (currentState < 8) ? stateNames[currentState] : "UNKNOWN",
    (newState < 8) ? stateNames[newState] : "UNKNOWN");
  
  currentState = newState;
  screen.fillScreen(COLOR_RGB565_BLACK);

  switch (currentState) {
    case STATE_MAIN_MENU:
      DEBUG_PRINTLN("Drawing main menu");
      drawMainMenu();
      break;
    case STATE_MANUAL_RUN:
      // Reset zone selection
      selectedManualZoneIndex = 0;
      DEBUG_PRINTLN("Entering manual run mode");
      drawManualRunMenu();
      break;
    case STATE_SETTINGS:
      // Reset settings selection
      selectedSettingsMenuIndex = 0;
      DEBUG_PRINTLN("Entering settings menu");
      drawSettingsMenu();
      break;
    case STATE_SET_SYSTEM_TIME:
      // Start editing from the first field
      timeEditFieldIndex = 0;
      DEBUG_PRINTLN("Entering system time setting mode");
      drawSetSystemTimeMenu();
      break;
    case STATE_WIFI_SETUP:
      DEBUG_PRINTLN("Starting WiFi setup");
      startWiFiSetup();
      break;
    case STATE_WIFI_RESET:
      DEBUG_PRINTLN("Resetting WiFi credentials");
      drawWiFiResetMenu();
      break;
    case STATE_SYSTEM_INFO:
      DEBUG_PRINTLN("Displaying system information");
      drawSystemInfoMenu();
      break;
    case STATE_SET_CYCLE_START:
      // Start editing from the first field
      cycleEditFieldIndex = 0;
      DEBUG_PRINTLN("Entering cycle start time setting mode");
      drawSetCycleStartMenu();
      break;
    case STATE_PROG_A:
      programEditZoneIndex = 0;
      DEBUG_PRINTLN("Entering Program A configuration");
      drawProgramConfigMenu("Program A", programA);
      break;
    case STATE_PROG_B:
      programEditZoneIndex = 0;
      DEBUG_PRINTLN("Entering Program B configuration");
      drawProgramConfigMenu("Program B", programB);
      break;
    case STATE_PROG_C:
      programEditZoneIndex = 0;
      DEBUG_PRINTLN("Entering Program C configuration");
      drawProgramConfigMenu("Program C", programC);
      break;
    case STATE_RUNNING_ZONE:
      DEBUG_PRINTLN("Entering running zone state");
      drawRunningZoneMenu();
      break;
    case STATE_TEST_MODE:
      DEBUG_PRINTLN("Starting test mode");
      startTestMode();
      break;
    default:
      DEBUG_PRINTF("Unknown state entered: %d\n", currentState);
      break;
  }
}

// -----------------------------------------------------------------------------
//                           MAIN MENU DRAWING
// -----------------------------------------------------------------------------
void drawMainMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Show date/time at the top
  drawDateTime(10, 10);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 40);
  screen.println("Main Menu");

  // Draw each menu item
  for (int i = 0; i < MAIN_MENU_ITEMS; i++) {
    int yPos = 80 + i * 30;
    uint16_t color = (i == selectedMainMenuIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    screen.println(mainMenuLabels[i]);
  }
}

// A simple function to display the current date/time
void drawDateTime(int x, int y) {
  screen.setCursor(x, y);
  screen.setTextColor(COLOR_RGB565_GREEN);
  screen.setTextSize(2);

  // Format example: YYYY-MM-DD HH:MM:SS
  char buf[32];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
      currentDateTime.year,
      currentDateTime.month,
      currentDateTime.day,
      currentDateTime.hour,
      currentDateTime.minute,
      currentDateTime.second
  );
  screen.println(buf);
}

// -----------------------------------------------------------------------------
//                           LOGO DISPLAY
// -----------------------------------------------------------------------------
void drawLogo() {
  screen.fillScreen(COLOR_RGB565_WHITE);
  
  // Calculate position to center the logo
  int x = (320 - LOGO_WIDTH) / 2;
  int y = (240 - LOGO_HEIGHT) / 2;
  
  // Draw the logo image
  for (int row = 0; row < LOGO_HEIGHT; row++) {
    for (int col = 0; col < LOGO_WIDTH; col++) {
      uint16_t pixel = logo_data[row * LOGO_WIDTH + col];
      screen.drawPixel(x + col, y + row, pixel);
    }
  }
  
  // Add version info below the logo
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_BLACK);
  screen.setCursor(80, y + LOGO_HEIGHT + 20);
  screen.println("v1.0 - ESP32-C6");
  
  // Loading indicator
  screen.setCursor(90, y + LOGO_HEIGHT + 40);
  screen.println("Loading...");
}

// -----------------------------------------------------------------------------
//                         SIMPLE SOFTWARE CLOCK
// -----------------------------------------------------------------------------
void updateSoftwareClock() {
  unsigned long now = millis();
  if ((now - lastSecondUpdate) >= 1000) {
    lastSecondUpdate = now;
    incrementOneSecond();

    // If in main menu, re-draw date/time so it remains fresh
    if (currentState == STATE_MAIN_MENU) {
      // Overwrite old area
      screen.fillRect(10, 10, 300, 20, COLOR_RGB565_BLACK);
      // Re-draw date/time
      drawDateTime(10, 10);
    }
  }
}

// -----------------------------------------------------------------------------
//                           MANUAL RUN FUNCTIONS
// -----------------------------------------------------------------------------
void drawManualRunMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Manual Run");

  // Print instructions
  screen.setCursor(10, 40);
  screen.setTextColor(COLOR_RGB565_RED);
  screen.println("Select Zone & Press Button");

  // List zones
  for (int i = 0; i < ZONE_COUNT; i++) {
    int yPos = 80 + i * 30;
    uint16_t color = (i == selectedManualZoneIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    // zone i => relay i+1
    screen.print(relayLabels[i+1]);
    screen.print(": ");
    screen.println(relayStates[i+1] ? "ON" : "OFF");
  }
}

void startManualZone(int zoneIdx) {
  DEBUG_PRINTF("=== STARTING MANUAL ZONE %d ===\n", zoneIdx);
  DEBUG_PRINTF("Zone name: %s\n", relayLabels[zoneIdx]);
  DEBUG_PRINTF("Zone pin: %d\n", relayPins[zoneIdx]);

  // Turn off any previously running zone
  DEBUG_PRINTLN("Stopping all zones before starting new zone...");
  stopZone();

  // Switch ON the zone
  DEBUG_PRINTF("Activating zone %d relay (pin %d)\n", zoneIdx, relayPins[zoneIdx]);
  relayStates[zoneIdx] = true;
  digitalWrite(relayPins[zoneIdx], HIGH);

  // Switch ON the pump
  DEBUG_PRINTF("Activating pump relay (pin %d)\n", relayPins[PUMP_IDX]);
  relayStates[PUMP_IDX] = true;
  digitalWrite(relayPins[PUMP_IDX], HIGH);

  DEBUG_PRINTF("Zone %d and pump are now ACTIVE\n", zoneIdx);
  DEBUG_PRINTF("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Move to "running zone" state (indefinite or timed, your choice)
  enterState(STATE_RUNNING_ZONE);
}

void drawRunningZoneMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Title
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Zone Running");

  // Show current date/time
  drawDateTime(10, 40);

  // Find which zone is currently running
  int runningZone = -1;
  for (int i = 1; i < NUM_RELAYS; i++) {
    if (relayStates[i]) {
      runningZone = i;
      break;
    }
  }

  // Display running zone information
  screen.setTextSize(2);
  if (runningZone > 0) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setCursor(10, 80);
    screen.printf("Active: %s", relayLabels[runningZone]);
    
    // Show pump status
    screen.setCursor(10, 110);
    screen.setTextColor(relayStates[PUMP_IDX] ? COLOR_RGB565_GREEN : COLOR_RGB565_RED);
    screen.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");
  } else {
    screen.setTextColor(COLOR_RGB565_RED);
    screen.setCursor(10, 80);
    screen.println("No Zone Active");
  }

  // Show all zone status
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 150);
  screen.println("Zone Status:");

  for (int i = 1; i < NUM_RELAYS; i++) {
    int yPos = 170 + (i-1) * 12;
    screen.setCursor(10, yPos);
    
    // Highlight active zone
    if (relayStates[i]) {
      screen.setTextColor(COLOR_RGB565_GREEN);
    } else {
      screen.setTextColor(COLOR_RGB565_LGRAY);
    }
    
    screen.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
  }

  // Instructions
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 280);
  screen.println("Press button to stop zone");
}

void stopZone() {
  DEBUG_PRINTLN("=== STOPPING ALL ZONES ===");
  
  // Turn off all zones
  for (int i = 1; i < NUM_RELAYS; i++) {
    if (relayStates[i]) {
      DEBUG_PRINTF("Deactivating zone %d (%s) on pin %d\n", i, relayLabels[i], relayPins[i]);
    }
    relayStates[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
  
  // Turn off pump
  if (relayStates[PUMP_IDX]) {
    DEBUG_PRINTF("Deactivating pump on pin %d\n", relayPins[PUMP_IDX]);
  }
  relayStates[PUMP_IDX] = false;
  digitalWrite(relayPins[PUMP_IDX], LOW);
  
  DEBUG_PRINTLN("All zones and pump are now OFF");
}

// -----------------------------------------------------------------------------
//                       SET SYSTEM TIME (FULLY IMPLEMENTED)
// -----------------------------------------------------------------------------
void drawSetSystemTimeMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Set System Time");

  // We'll display each field (year, month, day, hour, minute, second)
  // and highlight the one currently being edited.
  screen.setTextSize(2);

  // Helper lambda for drawing a line with highlight
  auto drawField = [&](int lineIndex, const char* label, int value){
    int y = 60 + lineIndex * 30;
    uint16_t color = (lineIndex == timeEditFieldIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, y);
    char buf[32];
    sprintf(buf, "%s %d", label, value);
    screen.println(buf);
  };

  drawField(0, "Year  :", currentDateTime.year);
  drawField(1, "Month :", currentDateTime.month);
  drawField(2, "Day   :", currentDateTime.day);
  drawField(3, "Hour  :", currentDateTime.hour);
  drawField(4, "Min   :", currentDateTime.minute);
  drawField(5, "Sec   :", currentDateTime.second);

  // Instructions
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 260);
  screen.println("Rotate to change value, Press to next field.");
}

void handleSetSystemTimeEncoder(long diff) {
  // Modify the currently selected field
  switch(timeEditFieldIndex) {
    case 0: // Year
      currentDateTime.year += diff;
      if (currentDateTime.year < MIN_YEAR) currentDateTime.year = MIN_YEAR;
      if (currentDateTime.year > MAX_YEAR) currentDateTime.year = MAX_YEAR;
      break;
    case 1: // Month
      currentDateTime.month += diff;
      if (currentDateTime.month < 1) currentDateTime.month = 12;
      if (currentDateTime.month > 12) currentDateTime.month = 1;
      break;
    case 2: // Day
      currentDateTime.day += diff;
      if (currentDateTime.day < 1) currentDateTime.day = 31;
      if (currentDateTime.day > 31) currentDateTime.day = 1;
      break;
    case 3: // Hour
      currentDateTime.hour += diff;
      if (currentDateTime.hour < 0) currentDateTime.hour = 23;
      if (currentDateTime.hour > 23) currentDateTime.hour = 0;
      break;
    case 4: // Minute
      currentDateTime.minute += diff;
      if (currentDateTime.minute < 0) currentDateTime.minute = 59;
      if (currentDateTime.minute > 59) currentDateTime.minute = 0;
      break;
    case 5: // Second
      currentDateTime.second += diff;
      if (currentDateTime.second < 0) currentDateTime.second = 59;
      if (currentDateTime.second > 59) currentDateTime.second = 0;
      break;
    default:
      break;
  }
  // Redraw the screen with updated value
  drawSetSystemTimeMenu();
}

void handleSetSystemTimeButton() {
  // Move to the next field
  timeEditFieldIndex++;
  if (timeEditFieldIndex > 5) {
    // Done editing all fields
    timeEditFieldIndex = 0;
    enterState(STATE_MAIN_MENU);
  } else {
    drawSetSystemTimeMenu();
  }
}

// -----------------------------------------------------------------------------
//                      SET CYCLE START TIME (FULLY IMPLEMENTED)
// -----------------------------------------------------------------------------
void drawSetCycleStartMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Set Cycle Start");

  // We'll display hour and minute, highlight the selected one
  auto drawField = [&](int index, const char* label, int value) {
    int y = 60 + index * 30;
    uint16_t color = (index == cycleEditFieldIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, y);
    char buf[32];
    sprintf(buf, "%s %02d", label, value);
    screen.println(buf);
  };

  drawField(0, "Hour  :", cycleStartTime.hour);
  drawField(1, "Minute:", cycleStartTime.minute);

  // Instructions
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 120);
  screen.println("Rotate to change value, Press to next field.");
}

void handleSetCycleStartEncoder(long diff) {
  switch(cycleEditFieldIndex) {
    case 0: // hour
      cycleStartTime.hour += diff;
      if (cycleStartTime.hour < 0)  cycleStartTime.hour = 23;
      if (cycleStartTime.hour > 23) cycleStartTime.hour = 0;
      break;
    case 1: // minute
      cycleStartTime.minute += diff;
      if (cycleStartTime.minute < 0)  cycleStartTime.minute = 59;
      if (cycleStartTime.minute > 59) cycleStartTime.minute = 0;
      break;
    default:
      break;
  }
  drawSetCycleStartMenu();
}

void handleSetCycleStartButton() {
  cycleEditFieldIndex++;
  if (cycleEditFieldIndex > 1) {
    cycleEditFieldIndex = 0;
    enterState(STATE_MAIN_MENU);
  } else {
    drawSetCycleStartMenu();
  }
}

// -----------------------------------------------------------------------------
//          PROGRAM A / B / C CONFIG EDIT (FULLY IMPLEMENTED)
// -----------------------------------------------------------------------------
void drawProgramConfigMenu(const char* label, ProgramConfig& cfg) {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.print(label);
  screen.println(" Configuration");

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_WHITE);

  // Draw zone durations
  for (int i = 0; i < ZONE_COUNT; i++) {
    int yPos = 60 + i * 25;
    // If programEditZoneIndex == i, highlight
    uint16_t color = (programEditZoneIndex == i) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    char buf[50];
    sprintf(buf, "Zone %d: %d min", i+1, cfg.zoneDurations[i]);
    screen.println(buf);
  }

  // Draw interZoneDelay
  {
    int i = ZONE_COUNT; // index 7 for the delay
    int yPos = 60 + i * 25;
    uint16_t color = (programEditZoneIndex == i) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    char buf[50];
    sprintf(buf, "Delay: %d min", cfg.interZoneDelay);
    screen.println(buf);
  }

  // Instructions
  screen.setCursor(10, 250);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setTextSize(1);
  screen.println("Rotate to change value, Press to next field.");
  screen.println("After last field => returns to Main Menu.");
}

void handleProgramEditEncoder(long diff, ProgramConfig &cfg, const char* progLabel) {
  if (programEditZoneIndex < ZONE_COUNT) {
    // editing zone durations
    int newDur = cfg.zoneDurations[programEditZoneIndex] + diff;
    if (newDur < 0)   newDur = 0;
    if (newDur > 120) newDur = 120;  // limit to 120 min for demo
    cfg.zoneDurations[programEditZoneIndex] = newDur;
  } else {
    // editing interZoneDelay (the last field, index = 7)
    int newDelay = cfg.interZoneDelay + diff;
    if (newDelay < 0)  newDelay = 0;
    if (newDelay > 30) newDelay = 30; // limit to 30 min for demo
    cfg.interZoneDelay = newDelay;
  }
  drawProgramConfigMenu(progLabel, cfg);
}

void handleProgramEditButton(ProgramConfig &cfg, ProgramState thisState, const char* progLabel) {
  programEditZoneIndex++;
  if (programEditZoneIndex > ZONE_COUNT) {
    // We have 7 zones + 1 delay => total 8 fields => last index is 7
    // If we've advanced beyond 7 => done
    programEditZoneIndex = 0;
    enterState(STATE_MAIN_MENU);
  } else {
    drawProgramConfigMenu(progLabel, cfg);
  }
}

// -----------------------------------------------------------------------------
//                           WiFi and NTP Functions
// -----------------------------------------------------------------------------
void initWiFi() {
  DEBUG_PRINTLN("=== WiFi Initialization with WiFiManager ===");
  
  // Initialize preferences
  preferences.begin("irrigation", false);
  
  // Create WiFiManager instance
  WiFiManager wm;
  
  // Set debug output
  wm.setDebugOutput(DEBUG_ENABLED);
  
  // Set timeout for configuration portal (3 minutes)
  wm.setConfigPortalTimeout(180);
  
  // Set custom AP name and password
  wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  
  // Display WiFi setup message on screen
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("WiFi Setup");
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 50);
  screen.println("Connecting...");
  
  DEBUG_PRINTLN("Attempting to connect to saved WiFi...");
  
  // Try to connect with saved credentials first
  if (!wm.autoConnect("IrrigationController", "irrigation123")) {
    DEBUG_PRINTLN("Failed to connect to WiFi");
    
    // Show captive portal instructions on display
    screen.fillScreen(COLOR_RGB565_BLACK);
    screen.setTextSize(2);
    screen.setTextColor(COLOR_RGB565_RED);
    screen.setCursor(10, 10);
    screen.println("WiFi Setup Required");
    
    screen.setTextSize(1);
    screen.setTextColor(COLOR_RGB565_WHITE);
    screen.setCursor(10, 50);
    screen.println("1. Connect to WiFi:");
    screen.setCursor(10, 70);
    screen.println("   'IrrigationController'");
    screen.setCursor(10, 90);
    screen.println("2. Password: irrigation123");
    screen.setCursor(10, 110);
    screen.println("3. Open browser to:");
    screen.setCursor(10, 130);
    screen.println("   192.168.4.1");
    screen.setCursor(10, 150);
    screen.println("4. Configure your WiFi");
    screen.setCursor(10, 170);
    screen.println("5. Device will restart");
    
    screen.setTextColor(COLOR_RGB565_YELLOW);
    screen.setCursor(10, 200);
    screen.println("Waiting for config...");
    
    wifiConnected = false;
    DEBUG_PRINTLN("WiFi configuration portal started");
    DEBUG_PRINTLN("Connect to 'IrrigationController' AP");
    DEBUG_PRINTLN("Password: irrigation123");
    DEBUG_PRINTLN("Open browser to 192.168.4.1");
    
    // If we reach here, either config was successful or timed out
    if (WiFi.status() == WL_CONNECTED) {
      connectToWiFi();
    } else {
      DEBUG_PRINTLN("WiFi configuration timed out - continuing without WiFi");
      screen.fillScreen(COLOR_RGB565_BLACK);
      screen.setTextSize(2);
      screen.setTextColor(COLOR_RGB565_RED);
      screen.setCursor(10, 10);
      screen.println("WiFi Setup");
      screen.println("Timed Out");
      screen.setTextColor(COLOR_RGB565_WHITE);
      screen.setCursor(10, 80);
      screen.println("Continuing without");
      screen.println("internet time sync");
      delay(3000);
    }
  } else {
    // Successfully connected
    connectToWiFi();
  }
}

void connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    DEBUG_PRINTLN("WiFi connected successfully!");
    DEBUG_PRINTF("SSID: %s\n", WiFi.SSID().c_str());
    DEBUG_PRINTF("IP address: %s\n", WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("Signal strength: %d dBm\n", WiFi.RSSI());
    
    // Show success on display
    screen.fillScreen(COLOR_RGB565_BLACK);
    screen.setTextSize(2);
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setCursor(10, 10);
    screen.println("WiFi Connected!");
    
    screen.setTextSize(1);
    screen.setTextColor(COLOR_RGB565_WHITE);
    screen.setCursor(10, 50);
    screen.printf("SSID: %s\n", WiFi.SSID().c_str());
    screen.setCursor(10, 70);
    screen.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    screen.setCursor(10, 90);
    screen.println("Syncing time...");
    
    // Initialize NTP
    syncTimeWithNTP();
    
    // Show final status
    screen.setCursor(10, 110);
    if (timeSync) {
      screen.setTextColor(COLOR_RGB565_GREEN);
      screen.println("Time sync: SUCCESS");
    } else {
      screen.setTextColor(COLOR_RGB565_YELLOW);
      screen.println("Time sync: FAILED");
    }
    
    delay(2000);
  } else {
    wifiConnected = false;
    DEBUG_PRINTLN("WiFi connection failed");
  }
}

void syncTimeWithNTP() {
  if (!wifiConnected) {
    DEBUG_PRINTLN("Cannot sync time - WiFi not connected");
    return;
  }
  
  DEBUG_PRINTLN("Initializing NTP time synchronization...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait for time to be set
  int attempts = 0;
  while (!time(nullptr) && attempts < 10) {
    DEBUG_PRINT(".");
    delay(1000);
    attempts++;
  }
  
  if (time(nullptr)) {
    timeSync = true;
    lastNTPSync = millis();
    updateSystemTimeFromNTP();
    DEBUG_PRINTLN("\nNTP time synchronization successful!");
  } else {
    DEBUG_PRINTLN("\nFailed to synchronize with NTP server");
  }
}

void updateTimeFromNTP() {
  // Check if it's time to sync again
  if (wifiConnected && timeSync && (millis() - lastNTPSync > NTP_SYNC_INTERVAL)) {
    DEBUG_PRINTLN("Performing periodic NTP sync...");
    syncTimeWithNTP();
  }
}

void updateSystemTimeFromNTP() {
  if (!timeSync) return;
  
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  // Update our system time structure
  currentDateTime.year = timeinfo.tm_year + 1900;
  currentDateTime.month = timeinfo.tm_mon + 1;
  currentDateTime.day = timeinfo.tm_mday;
  currentDateTime.hour = timeinfo.tm_hour;
  currentDateTime.minute = timeinfo.tm_min;
  currentDateTime.second = timeinfo.tm_sec;
  
  DEBUG_PRINTF("System time updated from NTP: %04d-%02d-%02d %02d:%02d:%02d\n",
    currentDateTime.year, currentDateTime.month, currentDateTime.day,
    currentDateTime.hour, currentDateTime.minute, currentDateTime.second);
}

// -----------------------------------------------------------------------------
//                           Settings Menu Functions
// -----------------------------------------------------------------------------
void drawSettingsMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Settings");

  // Show WiFi status indicator
  screen.setTextSize(1);
  screen.setCursor(10, 40);
  if (wifiConnected) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.printf("WiFi: Connected (%s)", WiFi.SSID().c_str());
  } else {
    screen.setTextColor(COLOR_RGB565_RED);
    screen.println("WiFi: Not Connected");
  }

  // Show time sync status
  screen.setCursor(10, 55);
  if (timeSync) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.println("Time: NTP Synced");
  } else {
    screen.setTextColor(COLOR_RGB565_YELLOW);
    screen.println("Time: Manual/Software Clock");
  }

  // Draw each settings menu item
  screen.setTextSize(2);
  for (int i = 0; i < SETTINGS_MENU_ITEMS; i++) {
    int yPos = 90 + i * 30;
    uint16_t color = (i == selectedSettingsMenuIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    screen.println(settingsMenuLabels[i]);
  }

  // Instructions
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 250);
  screen.println("Rotate to select, Press to confirm");
}

void startWiFiSetup() {
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("WiFi Setup");

  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 50);
  screen.println("Starting WiFi");
  screen.println("configuration...");

  DEBUG_PRINTLN("=== Manual WiFi Setup ===");
  
  // Create WiFiManager instance
  WiFiManager wm;
  wm.setDebugOutput(DEBUG_ENABLED);
  wm.setConfigPortalTimeout(180); // 3 minutes
  
  // Show setup instructions
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("WiFi Setup Portal");
  
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 50);
  screen.println("1. Connect to WiFi:");
  screen.setCursor(10, 70);
  screen.println("   'IrrigationController'");
  screen.setCursor(10, 90);
  screen.println("2. Password: irrigation123");
  screen.setCursor(10, 110);
  screen.println("3. Open browser to:");
  screen.setCursor(10, 130);
  screen.println("   192.168.4.1");
  screen.setCursor(10, 150);
  screen.println("4. Configure your WiFi");
  
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 180);
  screen.println("Starting portal...");
  
  // Start configuration portal
  if (wm.startConfigPortal("IrrigationController", "irrigation123")) {
    DEBUG_PRINTLN("WiFi configuration successful!");
    wifiConnected = true;
    connectToWiFi();
  } else {
    DEBUG_PRINTLN("WiFi configuration failed or timed out");
    screen.fillScreen(COLOR_RGB565_BLACK);
    screen.setTextSize(2);
    screen.setTextColor(COLOR_RGB565_RED);
    screen.setCursor(10, 10);
    screen.println("WiFi Setup");
    screen.println("Failed");
    screen.setTextColor(COLOR_RGB565_WHITE);
    screen.setCursor(10, 80);
    screen.println("Try again later");
    delay(2000);
  }
  
  // Return to settings menu
  enterState(STATE_SETTINGS);
}

void drawWiFiResetMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("WiFi Reset");

  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 50);
  screen.println("Clearing saved");
  screen.println("WiFi credentials...");

  DEBUG_PRINTLN("=== WiFi Reset ===");
  resetWiFiCredentials();

  screen.setTextColor(COLOR_RGB565_GREEN);
  screen.setCursor(10, 120);
  screen.println("WiFi credentials");
  screen.println("cleared!");

  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 180);
  screen.println("Use 'WiFi Setup' to");
  screen.println("configure new network");

  delay(3000);
  enterState(STATE_SETTINGS);
}

void drawSystemInfoMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("System Info");

  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  
  int y = 50;
  screen.setCursor(10, y);
  screen.println("=== Hardware ===");
  y += 15;
  
  screen.setCursor(10, y);
  screen.println("Board: ESP32-C6");
  y += 12;
  
  screen.setCursor(10, y);
  screen.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  y += 12;
  
  screen.setCursor(10, y);
  screen.printf("Chip Rev: %d\n", ESP.getChipRevision());
  y += 20;

  screen.setCursor(10, y);
  screen.println("=== Network ===");
  y += 15;
  
  if (wifiConnected) {
    screen.setCursor(10, y);
    screen.printf("SSID: %s\n", WiFi.SSID().c_str());
    y += 12;
    
    screen.setCursor(10, y);
    screen.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    y += 12;
    
    screen.setCursor(10, y);
    screen.printf("Signal: %d dBm\n", WiFi.RSSI());
    y += 12;
    
    screen.setCursor(10, y);
    screen.printf("MAC: %s\n", WiFi.macAddress().c_str());
    y += 20;
  } else {
    screen.setCursor(10, y);
    screen.println("WiFi: Not Connected");
    y += 20;
  }

  screen.setCursor(10, y);
  screen.println("=== Time ===");
  y += 15;
  
  screen.setCursor(10, y);
  if (timeSync) {
    screen.println("Source: NTP Server");
    y += 12;
    screen.setCursor(10, y);
    screen.printf("Last Sync: %lu min ago\n", (millis() - lastNTPSync) / 60000);
  } else {
    screen.println("Source: Software Clock");
  }

  // Instructions
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 220);
  screen.println("Press button to return");

  // Wait for button press
  while (digitalRead(button) == HIGH) {
    delay(50);
  }
  delay(200); // Debounce
  
  enterState(STATE_SETTINGS);
}

void resetWiFiCredentials() {
  DEBUG_PRINTLN("Clearing WiFi credentials from preferences...");
  preferences.remove(WIFI_SSID_KEY);
  preferences.remove(WIFI_PASS_KEY);
  
  // Also clear WiFiManager saved credentials
  WiFiManager wm;
  wm.resetSettings();
  
  // Disconnect current WiFi
  WiFi.disconnect(true);
  wifiConnected = false;
  timeSync = false;
  
  DEBUG_PRINTLN("WiFi credentials cleared successfully");
}

// -----------------------------------------------------------------------------
//                           TEST MODE FUNCTIONS
// -----------------------------------------------------------------------------
void startTestMode() {
  DEBUG_PRINTLN("=== STARTING TEST MODE ===");
  
  // Initialize test mode variables
  testModeActive = true;
  currentTestRelay = 0;  // Start with pump (relay 0)
  testModeStartTime = millis();
  
  // Turn off all relays first
  stopZone();
  
  // Turn on the first relay (pump)
  DEBUG_PRINTF("Turning on relay %d (%s)\n", currentTestRelay, relayLabels[currentTestRelay]);
  relayStates[currentTestRelay] = true;
  digitalWrite(relayPins[currentTestRelay], HIGH);
  
  // Draw initial test mode screen
  drawTestModeMenu();
  
  DEBUG_PRINTLN("Test mode initialized - pump is now ON");
}

void updateTestMode() {
  if (!testModeActive) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - testModeStartTime;
  
  // Check if it's time to switch to the next relay
  if (elapsed >= TEST_INTERVAL) {
    // Turn off current relay
    if (currentTestRelay < NUM_RELAYS) {
      DEBUG_PRINTF("Turning off relay %d (%s)\n", currentTestRelay, relayLabels[currentTestRelay]);
      relayStates[currentTestRelay] = false;
      digitalWrite(relayPins[currentTestRelay], LOW);
    }
    
    // Move to next relay
    currentTestRelay++;
    
    // Check if we've tested all relays
    if (currentTestRelay >= NUM_RELAYS) {
      DEBUG_PRINTLN("Test mode complete - all relays tested");
      stopTestMode();
      enterState(STATE_MAIN_MENU);
      return;
    }
    
    // Turn on next relay
    DEBUG_PRINTF("Turning on relay %d (%s)\n", currentTestRelay, relayLabels[currentTestRelay]);
    relayStates[currentTestRelay] = true;
    digitalWrite(relayPins[currentTestRelay], HIGH);
    
    // Reset timer for next interval
    testModeStartTime = currentTime;
    
    // Update display
    drawTestModeMenu();
  }
}

void drawTestModeMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);
  
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Test Mode");
  
  // Show current status
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 50);
  
  if (currentTestRelay < NUM_RELAYS) {
    screen.printf("Testing: %s", relayLabels[currentTestRelay]);
    
    // Show countdown
    unsigned long elapsed = millis() - testModeStartTime;
    unsigned long remaining = (TEST_INTERVAL - elapsed) / 1000;
    
    screen.setCursor(10, 80);
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.printf("Time left: %lu sec", remaining);
    
    // Show progress
    screen.setCursor(10, 110);
    screen.setTextColor(COLOR_RGB565_CYAN);
    screen.printf("Relay %d of %d", currentTestRelay + 1, NUM_RELAYS);
  } else {
    screen.println("Test Complete!");
  }
  
  // Show all relay states
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 150);
  screen.println("Relay Status:");
  
  for (int i = 0; i < NUM_RELAYS; i++) {
    int yPos = 170 + i * 12;
    screen.setCursor(10, yPos);
    
    // Highlight current relay
    if (i == currentTestRelay && testModeActive) {
      screen.setTextColor(COLOR_RGB565_GREEN);
    } else {
      screen.setTextColor(COLOR_RGB565_LGRAY);
    }
    
    screen.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
  }
  
  // Instructions
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 280);
  screen.println("Press button to cancel test");
}

void stopTestMode() {
  DEBUG_PRINTLN("=== STOPPING TEST MODE ===");
  
  testModeActive = false;
  
  // Turn off all relays
  stopZone();
  
  DEBUG_PRINTLN("Test mode stopped - all relays OFF");
}
