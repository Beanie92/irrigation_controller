#include <Arduino.h>
#include "DFRobot_GDL.h"
#include "logo.h"
#include "ui_components.h"
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
#define TFT_BL   5 // Backlight control pin for dimming

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
//                           Screen Dimming
// -----------------------------------------------------------------------------
unsigned long lastActivityTime = 0;
const unsigned long inactivityTimeout = 30000; // 30 seconds
bool isScreenDimmed = false;

// -----------------------------------------------------------------------------
//                           Menu and Program States
// -----------------------------------------------------------------------------
enum ActiveOperationType {
  OP_NONE,
  OP_MANUAL_ZONE,
  OP_MANUAL_PROGRAM,
  OP_SCHEDULED_PROGRAM
};

enum UIState {
  STATE_MAIN_MENU,
  STATE_MANUAL_RUN,        
  STATE_PROGRAMS_MENU,
  STATE_PROGRAM_A_MENU,
  STATE_PROGRAM_B_MENU,
  STATE_PROGRAM_C_MENU,
  STATE_SETTINGS,
  STATE_SET_SYSTEM_TIME,
  STATE_WIFI_SETUP,
  STATE_WIFI_RESET,
  STATE_SYSTEM_INFO,
  STATE_PROG_A,
  STATE_PROG_B,
  STATE_PROG_C,
  STATE_RUNNING_ZONE,
  STATE_PROGRAM_RUNNING,
  STATE_TEST_MODE
};

UIState currentState = STATE_MAIN_MENU;
const int UI_STATE_STACK_SIZE = 10;
UIState uiStateStack[UI_STATE_STACK_SIZE];
int uiStateStackPtr = -1;
ActiveOperationType currentOperation = OP_NONE; // Track what kind of operation is active

// Main Menu Items
static const int MAIN_MENU_ITEMS = 4; 
const char* mainMenuLabels[MAIN_MENU_ITEMS] = {
  "Manual Run",
  "Programs", // New submenu
  "Test Mode",
  "Settings"
};
int selectedMainMenuIndex = 0; 

// Programs Menu Items (lists Program A, B, C)
static const int PROGRAMS_MENU_ITEMS = 3; // A, B, C
const char* programsMenuLabels[PROGRAMS_MENU_ITEMS] = {
  "Program A",
  "Program B",
  "Program C"
};
int selectedProgramsMenuIndex = 0;

// Individual Program Sub-Menu Items (Run Now, Configure)
static const int PROGRAM_SUB_MENU_ITEMS = 2; // Run Now, Configure
const char* programSubMenuLabels[PROGRAM_SUB_MENU_ITEMS] = {
  "Run Now",
  "Configure"
};
int selectedProgramSubMenuIndex = 0;
ScrollableList programSubMenuScrollList;

// Settings Menu Items
static const int SETTINGS_MENU_ITEMS = 4;
const char* settingsMenuLabels[SETTINGS_MENU_ITEMS] = {
  "WiFi Setup",
  "Set Time Manually",
  "WiFi Reset",
  "System Info"
};
int selectedSettingsMenuIndex = 0;
ScrollableList settingsMenuScrollList;

// Manual Run zone index: 0..6 => zone = index+1
int selectedManualZoneIndex = 0;
int selectedManualDuration = 5;  // Default 5 minutes
bool selectingDuration = false;  // Whether we're selecting duration or zone
ScrollableList manualRunScrollList;

// Program Config zone selection
int selectedProgramZoneIndex = 0; // Used by all program config screens
bool editingProgramZone = false; // Are we editing a zone duration?

// -----------------------------------------------------------------------------
//                           Zone Timer Variables
// -----------------------------------------------------------------------------
unsigned long zoneStartTime = 0;        // When current zone started
unsigned long zoneDuration = 0;         // Duration for current zone in milliseconds
int currentRunningZone = -1;            // Which zone is currently running (-1 = none)
bool isTimedRun = false;                 // Whether this is a timed run or manual indefinite run
int currentRunningProgram = -1;         // Which program is currently running (-1 = none, 0=A, 1=B, 2=C)
int currentProgramZoneIndex = -1;       // Current zone index within a running program
unsigned long programZoneStartTime = 0; // When current program zone started
unsigned long programInterZoneDelayStartTime = 0; // When inter-zone delay started
bool inInterZoneDelay = false;          // Flag for inter-zone delay

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
//                 Program Config
// -----------------------------------------------------------------------------
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
    .enabled = false,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program B"
};
ProgramConfig programC = {
    .enabled = false,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program C"
};

ProgramConfig* programs[] = {&programA, &programB, &programC};
static const int NUM_PROGRAMS = 3;

// -----------------------------------------------------------------------------
//                Sub-indexes and helpers for editing fields
// -----------------------------------------------------------------------------
static int timeEditFieldIndex = 0;    // 0=year,1=month,2=day,3=hour,4=minute,5=second
static bool editingTimeField = false;
static int programEditFieldIndex = 0; // 0=enabled, 1=hour, 2=minute, 3=interZoneDelay, 4-10=days, 11-17=zone durations
static int zoneEditScrollOffset = 0; // For scrolling through zones in program config

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

// Global instance for main menu
ScrollableList mainMenuScrollList;
ScrollableList programsMenuScrollList;
ScrollableList programZonesScrollList; // For zone durations in program config
ScrollableList setTimeScrollList;

// -----------------------------------------------------------------------------
//                           Forward Declarations
// -----------------------------------------------------------------------------
void isrPinA();
void handleEncoderMovement();
void handleButtonPress();

void drawLogo();
void drawMainMenu();
void drawProgramsMenu();
void drawProgramSubMenu(const char* label);
void navigateTo(UIState newState, bool isNavigatingBack = false);
void goBack();

void updateSoftwareClock();
DayOfWeek getCurrentDayOfWeek();

// Manual Run
void drawManualRunMenu();
void drawRunningZoneMenu();
void startManualZone(int zoneIdx);
void stopAllActivity(); // Renamed from stopZone

// Set System Time
void drawSetSystemTimeMenu();
void handleSetSystemTimeEncoder(long diff);
void handleSetSystemTimeButton();

// Program A/B/C
void drawProgramConfigMenu(const char* label, ProgramConfig& cfg);
void handleProgramEditEncoder(long diff, ProgramConfig &cfg, const char* progLabel);
void handleProgramEditButton(ProgramConfig &cfg, UIState thisState, const char* progLabel);
void startProgramRun(int programIndex, ActiveOperationType type);
void updateProgramRun();
void drawProgramRunningMenu();

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

  // Setup backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Full brightness initially
  lastActivityTime = millis();

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
  navigateTo(STATE_MAIN_MENU);
  DEBUG_PRINTLN("=== STARTUP COMPLETE ===");
}

// -----------------------------------------------------------------------------
//                                     LOOP
// -----------------------------------------------------------------------------
void loop() {
  // Handle screen dimming
  if (!isScreenDimmed && (millis() - lastActivityTime > inactivityTimeout)) {
    DEBUG_PRINTLN("Screen turning off due to inactivity.");
    digitalWrite(TFT_BL, LOW); // Turn off backlight
    isScreenDimmed = true;
  }

  // Update time - use NTP if available, otherwise software clock
  if (timeSync) {
    updateTimeFromNTP(); // Check for periodic NTP sync
    updateSystemTimeFromNTP(); // Update our time structure from system time
  } else {
    updateSoftwareClock(); // Fallback to software clock
  }
  
  handleEncoderMovement();
  handleButtonPress();

  // Check for scheduled programs if no other operation is active
  if (currentOperation == OP_NONE) {
    DayOfWeek currentDay = getCurrentDayOfWeek();
    for (int i = 0; i < NUM_PROGRAMS; i++) {
      ProgramConfig* cfg = programs[i];
      if (cfg->enabled && (cfg->daysActive & currentDay) &&
          currentDateTime.hour == cfg->startTime.hour &&
          currentDateTime.minute == cfg->startTime.minute &&
          currentDateTime.second == 0) { // Trigger at the start of the minute
        DEBUG_PRINTF("Scheduled program %s triggered!\n", cfg->name);
        startProgramRun(i, OP_SCHEDULED_PROGRAM);
        // Add a small delay to prevent re-triggering in the same second
        delay(1000); 
        break; // Only one program can start per second
      }
    }
  }

  // Additional logic for running states if needed
  if (currentState == STATE_RUNNING_ZONE) {
    // Update display every 5 seconds to show current timing
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 5000) {
      lastDisplayUpdate = millis();
      drawRunningZoneMenu();
    }
    
    // Check for automatic zone timeout if it's a timed run
    if (isTimedRun && zoneDuration > 0) {
      unsigned long elapsed = millis() - zoneStartTime;
      if (elapsed >= zoneDuration) {
        DEBUG_PRINTLN("Zone timer expired - stopping zone");
        stopAllActivity();
        navigateTo(STATE_MAIN_MENU);
      }
    }
  } else if (currentState == STATE_PROGRAM_RUNNING) {
    updateProgramRun();
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

  // Restore screen brightness on activity
  if (isScreenDimmed) {
    digitalWrite(TFT_BL, HIGH); // Full brightness
    isScreenDimmed = false;
    DEBUG_PRINTLN("Screen brightness restored.");
  }
  lastActivityTime = millis();

  DEBUG_PRINTF("Encoder moved: diff=%ld, state=%d\n", diff, currentState);

  switch (currentState) {
    case STATE_MAIN_MENU:
      handleScrollableListInput(mainMenuScrollList, diff);
      DEBUG_PRINTF("Main menu selection: %d (%s)\n", selectedMainMenuIndex, mainMenuLabels[selectedMainMenuIndex]);
      drawMainMenu();
      break;

    case STATE_PROGRAMS_MENU:
      handleScrollableListInput(programsMenuScrollList, diff);
      DEBUG_PRINTF("Programs menu selection: %d (%s)\n", selectedProgramsMenuIndex, programsMenuLabels[selectedProgramsMenuIndex]);
      drawProgramsMenu();
      break;

    case STATE_PROGRAM_A_MENU:
    case STATE_PROGRAM_B_MENU:
    case STATE_PROGRAM_C_MENU:
      handleScrollableListInput(programSubMenuScrollList, diff);
      {
        const char* progLabel;
        if (currentState == STATE_PROGRAM_A_MENU) progLabel = "Program A";
        else if (currentState == STATE_PROGRAM_B_MENU) progLabel = "Program B";
        else progLabel = "Program C";
        DEBUG_PRINTF("Program sub-menu selection: %d (%s)\n", selectedProgramSubMenuIndex, programSubMenuLabels[selectedProgramSubMenuIndex]);
        drawProgramSubMenu(progLabel);
      }
      break;

    case STATE_MANUAL_RUN:
      if (selectingDuration) {
        selectedManualDuration += diff;
        if (selectedManualDuration < 1) selectedManualDuration = 120;
        if (selectedManualDuration > 120) selectedManualDuration = 1;
        DEBUG_PRINTF("Manual run duration selection: %d minutes\n", selectedManualDuration);
      } else {
        handleScrollableListInput(manualRunScrollList, diff);
        if (selectedManualZoneIndex < ZONE_COUNT) { // Check index is valid for a zone
          DEBUG_PRINTF("Manual run zone selection: %d (%s)\n", selectedManualZoneIndex, relayLabels[selectedManualZoneIndex + 1]);
        } else {
          DEBUG_PRINTF("Manual run selection: Back button\n");
        }
      }
      drawManualRunMenu();
      break;

    case STATE_SETTINGS:
      handleScrollableListInput(settingsMenuScrollList, diff);
      DEBUG_PRINTF("Settings menu selection: %d (%s)\n", selectedSettingsMenuIndex, settingsMenuLabels[selectedSettingsMenuIndex]);
      drawSettingsMenu();
      break;

    case STATE_SET_SYSTEM_TIME:
      handleSetSystemTimeEncoder(diff);
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
    case STATE_PROGRAM_RUNNING:
      DEBUG_PRINTLN("Encoder ignored - zone/program is running");
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

      // Restore screen brightness on activity
      if (isScreenDimmed) {
        digitalWrite(TFT_BL, HIGH); // Full brightness
        isScreenDimmed = false;
        DEBUG_PRINTLN("Screen brightness restored.");
      }
      lastActivityTime = millis();

      // State-Specific Handling
      switch (currentState) {
        case STATE_MAIN_MENU:
          DEBUG_PRINTF("Main menu item selected: %d (%s)\n", selectedMainMenuIndex, mainMenuLabels[selectedMainMenuIndex]);
          switch (selectedMainMenuIndex) {
            case 0: navigateTo(STATE_MANUAL_RUN); break;
            case 1: navigateTo(STATE_PROGRAMS_MENU); break;
            case 2: navigateTo(STATE_TEST_MODE); break;
            case 3: navigateTo(STATE_SETTINGS); break;
          }
          break;

        case STATE_PROGRAMS_MENU:
          DEBUG_PRINTF("Programs menu item selected: %d (%s)\n", selectedProgramsMenuIndex, programsMenuLabels[selectedProgramsMenuIndex]);
          if (selectedProgramsMenuIndex == PROGRAMS_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedProgramsMenuIndex) {
              case 0: navigateTo(STATE_PROGRAM_A_MENU); break;
              case 1: navigateTo(STATE_PROGRAM_B_MENU); break;
              case 2: navigateTo(STATE_PROGRAM_C_MENU); break;
            }
          }
          break;

        case STATE_PROGRAM_A_MENU:
          DEBUG_PRINTF("Program A sub-menu item selected: %d (%s)\n", selectedProgramSubMenuIndex, programSubMenuLabels[selectedProgramSubMenuIndex]);
          if (selectedProgramSubMenuIndex == PROGRAM_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedProgramSubMenuIndex) {
              case 0: startProgramRun(0, OP_MANUAL_PROGRAM); break;
              case 1: navigateTo(STATE_PROG_A); break;
            }
          }
          break;

        case STATE_PROGRAM_B_MENU:
          DEBUG_PRINTF("Program B sub-menu item selected: %d (%s)\n", selectedProgramSubMenuIndex, programSubMenuLabels[selectedProgramSubMenuIndex]);
          if (selectedProgramSubMenuIndex == PROGRAM_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedProgramSubMenuIndex) {
              case 0: startProgramRun(1, OP_MANUAL_PROGRAM); break;
              case 1: navigateTo(STATE_PROG_B); break;
            }
          }
          break;

        case STATE_PROGRAM_C_MENU:
          DEBUG_PRINTF("Program C sub-menu item selected: %d (%s)\n", selectedProgramSubMenuIndex, programSubMenuLabels[selectedProgramSubMenuIndex]);
          if (selectedProgramSubMenuIndex == PROGRAM_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedProgramSubMenuIndex) {
              case 0: startProgramRun(2, OP_MANUAL_PROGRAM); break;
              case 1: navigateTo(STATE_PROG_C); break;
            }
          }
          break;

        case STATE_SETTINGS:
          DEBUG_PRINTF("Settings menu item selected: %d (%s)\n", selectedSettingsMenuIndex, settingsMenuLabels[selectedSettingsMenuIndex]);
          if (selectedSettingsMenuIndex == SETTINGS_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedSettingsMenuIndex) {
              case 0: navigateTo(STATE_WIFI_SETUP); break;
              case 1: navigateTo(STATE_SET_SYSTEM_TIME); break;
              case 2: navigateTo(STATE_WIFI_RESET); break;
              case 3: navigateTo(STATE_SYSTEM_INFO); break;
            }
          }
          break;

        case STATE_MANUAL_RUN:
          if (selectedManualZoneIndex >= ZONE_COUNT) { // Is the "Back" button selected?
            goBack();
          } else if (selectingDuration) {
            DEBUG_PRINTF("Starting manual zone: %d for %d minutes\n", selectedManualZoneIndex + 1, selectedManualDuration);
            startManualZone(selectedManualZoneIndex + 1); // zoneIdx 1..7
          } else {
            selectingDuration = true;
            DEBUG_PRINTF("Moving to duration selection for zone %d\n", selectedManualZoneIndex + 1);
            drawManualRunMenu();
          }
          break;

        case STATE_SET_SYSTEM_TIME:
          DEBUG_PRINTF("System time button - field %d\n", timeEditFieldIndex);
          handleSetSystemTimeButton();
          break;

        case STATE_PROG_A:
          DEBUG_PRINTF("Program A button - field %d\n", programEditFieldIndex);
          handleProgramEditButton(programA, STATE_PROG_A, "Program A");
          break;

        case STATE_PROG_B:
          DEBUG_PRINTF("Program B button - field %d\n", programEditFieldIndex);
          handleProgramEditButton(programB, STATE_PROG_B, "Program B");
          break;

        case STATE_PROG_C:
          DEBUG_PRINTF("Program C button - field %d\n", programEditFieldIndex);
          handleProgramEditButton(programC, STATE_PROG_C, "Program C");
          break;

        case STATE_RUNNING_ZONE:
        case STATE_PROGRAM_RUNNING:
          DEBUG_PRINTLN("Cancelling running zone/program");
          stopAllActivity();
          navigateTo(STATE_MAIN_MENU);
          break;

        case STATE_TEST_MODE:
          DEBUG_PRINTLN("Cancelling test mode");
          stopTestMode();
          navigateTo(STATE_MAIN_MENU);
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
void goBack() {
  if (uiStateStackPtr >= 0) {
    UIState lastState = uiStateStack[uiStateStackPtr--];
    DEBUG_PRINTF("Going back from %d to %d\n", currentState, lastState);
    navigateTo(lastState, true); // Navigate back, indicating it's a 'back' action
  } else {
    DEBUG_PRINTLN("State stack empty, cannot go back. Returning to main menu.");
    navigateTo(STATE_MAIN_MENU);
  }
}

void navigateTo(UIState newState, bool isNavigatingBack) {
  const char* stateNames[] = {
    "MAIN_MENU", "MANUAL_RUN", "PROGRAMS_MENU", "PROGRAM_A_MENU", "PROGRAM_B_MENU", "PROGRAM_C_MENU",
    "SETTINGS", "SET_SYSTEM_TIME", "WIFI_SETUP", "WIFI_RESET", "SYSTEM_INFO",
    "PROG_A", "PROG_B", "PROG_C", "RUNNING_ZONE", "PROGRAM_RUNNING", "TEST_MODE"
  };
  
  DEBUG_PRINTF("State transition: %s -> %s\n", 
    (currentState < sizeof(stateNames)/sizeof(stateNames[0])) ? stateNames[currentState] : "UNKNOWN",
    (newState < sizeof(stateNames)/sizeof(stateNames[0])) ? stateNames[newState] : "UNKNOWN");
  
  // Push the current state to the stack if we are navigating forward
  if (!isNavigatingBack) {
    if (currentState != STATE_PROGRAM_RUNNING && currentState != STATE_RUNNING_ZONE && currentState != STATE_TEST_MODE) {
      if (uiStateStackPtr < UI_STATE_STACK_SIZE - 1) {
        uiStateStack[++uiStateStackPtr] = currentState;
      } else {
        DEBUG_PRINTLN("UI state stack overflow!");
      }
    }
  }
  
  currentState = newState;
  screen.fillScreen(COLOR_RGB565_BLACK);

  switch (currentState) {
    case STATE_MAIN_MENU:
      mainMenuScrollList.items = mainMenuLabels;
      mainMenuScrollList.num_items = MAIN_MENU_ITEMS;
      mainMenuScrollList.selected_index_ptr = &selectedMainMenuIndex;
      mainMenuScrollList.x = 0;
      mainMenuScrollList.y = 70;
      mainMenuScrollList.width = 320;
      mainMenuScrollList.height = 240 - 70;
      mainMenuScrollList.item_text_size = 2;
      mainMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      mainMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      mainMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      mainMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      mainMenuScrollList.title = "Main Menu";
      mainMenuScrollList.title_text_size = 2;
      mainMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
      mainMenuScrollList.show_back_button = false; // No back button on main menu
      setupScrollableListMetrics(mainMenuScrollList, screen);
      DEBUG_PRINTLN("Drawing main menu");
      drawMainMenu();
      break;
    case STATE_MANUAL_RUN:
      selectedManualZoneIndex = 0;
      selectingDuration = false;
      manualRunScrollList.items = &relayLabels[1];
      manualRunScrollList.num_items = ZONE_COUNT;
      manualRunScrollList.selected_index_ptr = &selectedManualZoneIndex;
      manualRunScrollList.x = 0;
      manualRunScrollList.y = 0;
      manualRunScrollList.width = 320;
      manualRunScrollList.height = 240;
      manualRunScrollList.item_text_size = 2;
      manualRunScrollList.item_text_color = COLOR_RGB565_LGRAY;
      manualRunScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      manualRunScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      manualRunScrollList.list_bg_color = COLOR_RGB565_BLACK;
      manualRunScrollList.title = "Select Zone";
      manualRunScrollList.title_text_size = 2;
      manualRunScrollList.title_text_color = COLOR_RGB565_YELLOW;
      manualRunScrollList.show_back_button = true;
      setupScrollableListMetrics(manualRunScrollList, screen);
      DEBUG_PRINTLN("Entering manual run mode");
      drawManualRunMenu();
      break;
    case STATE_PROGRAMS_MENU:
      selectedProgramsMenuIndex = 0;
      programsMenuScrollList.items = programsMenuLabels;
      programsMenuScrollList.num_items = PROGRAMS_MENU_ITEMS;
      programsMenuScrollList.selected_index_ptr = &selectedProgramsMenuIndex;
      programsMenuScrollList.x = 0;
      programsMenuScrollList.y = 70;
      programsMenuScrollList.width = 320;
      programsMenuScrollList.height = 240 - 70;
      programsMenuScrollList.item_text_size = 2;
      programsMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      programsMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      programsMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      programsMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      programsMenuScrollList.title = "Programs";
      programsMenuScrollList.title_text_size = 2;
      programsMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
      programsMenuScrollList.show_back_button = true;
      setupScrollableListMetrics(programsMenuScrollList, screen);
      DEBUG_PRINTLN("Entering programs menu");
      drawProgramsMenu();
      break;
    case STATE_PROGRAM_A_MENU:
    case STATE_PROGRAM_B_MENU:
    case STATE_PROGRAM_C_MENU:
      selectedProgramSubMenuIndex = 0;
      {
        const char* progLabel;
        if (newState == STATE_PROGRAM_A_MENU) progLabel = "Program A";
        else if (newState == STATE_PROGRAM_B_MENU) progLabel = "Program B";
        else progLabel = "Program C";

        programSubMenuScrollList.items = programSubMenuLabels;
        programSubMenuScrollList.num_items = PROGRAM_SUB_MENU_ITEMS;
        programSubMenuScrollList.selected_index_ptr = &selectedProgramSubMenuIndex;
        programSubMenuScrollList.x = 0;
        programSubMenuScrollList.y = 70;
        programSubMenuScrollList.width = 320;
        programSubMenuScrollList.height = 240 - 70;
        programSubMenuScrollList.item_text_size = 2;
        programSubMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
        programSubMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
        programSubMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
        programSubMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
        programSubMenuScrollList.title = progLabel;
        programSubMenuScrollList.title_text_size = 2;
        programSubMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
        programSubMenuScrollList.show_back_button = true;
        setupScrollableListMetrics(programSubMenuScrollList, screen);
        DEBUG_PRINTF("Entering %s sub-menu\n", progLabel);
        drawProgramSubMenu(progLabel);
      }
      break;
    case STATE_SETTINGS:
      selectedSettingsMenuIndex = 0;
      settingsMenuScrollList.items = settingsMenuLabels;
      settingsMenuScrollList.num_items = SETTINGS_MENU_ITEMS;
      settingsMenuScrollList.selected_index_ptr = &selectedSettingsMenuIndex;
      settingsMenuScrollList.x = 0;
      settingsMenuScrollList.y = 50;
      settingsMenuScrollList.width = 320;
      settingsMenuScrollList.height = 240 - 50;
      settingsMenuScrollList.item_text_size = 2;
      settingsMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      settingsMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      settingsMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      settingsMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      settingsMenuScrollList.title = "Settings";
      settingsMenuScrollList.title_text_size = 2;
      settingsMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
      settingsMenuScrollList.show_back_button = true;
      setupScrollableListMetrics(settingsMenuScrollList, screen);
      DEBUG_PRINTLN("Entering settings menu");
      drawSettingsMenu();
      break;
    case STATE_SET_SYSTEM_TIME:
      timeEditFieldIndex = 0;
      editingTimeField = false;
      setTimeScrollList.num_items = 6; // 6 fields
      setTimeScrollList.selected_index_ptr = &timeEditFieldIndex;
      setTimeScrollList.x = 0;
      setTimeScrollList.y = 70;
      setTimeScrollList.width = 320;
      setTimeScrollList.height = 240 - 70;
      setTimeScrollList.item_text_size = 2;
      setTimeScrollList.item_text_color = COLOR_RGB565_LGRAY;
      setTimeScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      setTimeScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      setTimeScrollList.list_bg_color = COLOR_RGB565_BLACK;
      setTimeScrollList.title = "Set System Time";
      setTimeScrollList.title_text_size = 2;
      setTimeScrollList.title_text_color = COLOR_RGB565_YELLOW;
      setTimeScrollList.show_back_button = true;
      setupScrollableListMetrics(setTimeScrollList, screen);
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
    case STATE_PROG_A:
    case STATE_PROG_B:
    case STATE_PROG_C:
      {
        programEditFieldIndex = 0;
        editingProgramZone = false;
        selectedProgramZoneIndex = 0;

        ProgramConfig* currentProg;
        const char* progLabel;
        if (newState == STATE_PROG_A) { currentProg = &programA; progLabel = "Program A"; }
        else if (newState == STATE_PROG_B) { currentProg = &programB; progLabel = "Program B"; }
        else { currentProg = &programC; progLabel = "Program C"; }

        programZonesScrollList.data_source = currentProg->zoneDurations;
        programZonesScrollList.num_items = ZONE_COUNT;
        programZonesScrollList.selected_index_ptr = &selectedProgramZoneIndex;
        programZonesScrollList.format_string = "Zone %d: %d min";
        programZonesScrollList.x = 0;
        programZonesScrollList.y = 150;
        programZonesScrollList.width = 320;
        programZonesScrollList.height = 95;
        programZonesScrollList.item_text_size = 2;
        programZonesScrollList.item_text_color = COLOR_RGB565_LGRAY;
        programZonesScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
        programZonesScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
        programZonesScrollList.list_bg_color = COLOR_RGB565_BLACK;
        programZonesScrollList.title = "Zone Durations (min)";
        programZonesScrollList.title_text_size = 2;
        programZonesScrollList.title_text_color = COLOR_RGB565_YELLOW;
        programZonesScrollList.show_back_button = true;
        
        setupScrollableListMetrics(programZonesScrollList, screen);
        
        DEBUG_PRINTF("Entering %s configuration\n", progLabel);
        drawProgramConfigMenu(progLabel, *currentProg);
      }
      break;
    case STATE_RUNNING_ZONE:
      DEBUG_PRINTLN("Entering running zone state");
      drawRunningZoneMenu();
      break;
    case STATE_PROGRAM_RUNNING:
      DEBUG_PRINTLN("Entering program running state");
      drawProgramRunningMenu();
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
  // Show date/time at the top (outside the scrollable list component)
  drawDateTimeComponent(screen, 10, 10, currentDateTime, getCurrentDayOfWeek());
  
  // Draw the scrollable list for the main menu
  drawScrollableList(screen, mainMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           PROGRAMS MENU DRAWING
// -----------------------------------------------------------------------------
void drawProgramsMenu() {
  drawScrollableList(screen, programsMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           PROGRAM SUB-MENU DRAWING
// -----------------------------------------------------------------------------
void drawProgramSubMenu(const char* label) {
  programSubMenuScrollList.title = label;
  drawScrollableList(screen, programSubMenuScrollList, true);
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
    if (currentState == STATE_MAIN_MENU || currentState == STATE_PROGRAM_RUNNING || currentState == STATE_RUNNING_ZONE) {
      // Overwrite old area
      screen.fillRect(10, 10, 300, 20, COLOR_RGB565_BLACK);
      // Re-draw date/time
      drawDateTimeComponent(screen, 10, 10, currentDateTime, getCurrentDayOfWeek());
    }
  }
}

// Helper to get current day of week.
DayOfWeek getCurrentDayOfWeek() {
  if (timeSync) {
    // If synced with NTP, use the library's day of the week
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // tm_wday is 0 for Sunday, 1 for Monday, etc.
    // Our DayOfWeek enum is a bitfield, so we need to convert.
    return (DayOfWeek)(1 << timeinfo.tm_wday);
  } else {
    // If not synced, calculate day of week from date using Sakamoto's algorithm
    // https://en.wikipedia.org/wiki/Determination_of_the_day_of_the_week#Sakamoto's_methods
    int y = currentDateTime.year;
    int m = currentDateTime.month;
    int d = currentDateTime.day;
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
      y -= 1;
    }
    int dayOfWeekIndex = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7; // 0=Sunday, 1=Monday...
    return (DayOfWeek)(1 << dayOfWeekIndex);
  }
}

// -----------------------------------------------------------------------------
//                           MANUAL RUN FUNCTIONS
// -----------------------------------------------------------------------------
void drawManualRunMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  if (selectingDuration) {
    // Duration selection mode
    screen.setTextSize(2);
    screen.setTextColor(COLOR_RGB565_YELLOW);
    screen.setCursor(10, 10);
    screen.println("Manual Run");
    
    screen.setCursor(10, 40);
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.printf("Zone: %s", relayLabels[selectedManualZoneIndex + 1]);
    
    screen.setCursor(10, 70);
    screen.setTextColor(COLOR_RGB565_CYAN);
    screen.println("Select Duration:");
    
    screen.setCursor(10, 100);
    screen.setTextSize(3);
    screen.setTextColor(COLOR_RGB565_WHITE);
    screen.printf("%d minutes", selectedManualDuration);
    
    // Duration options for reference
    screen.setTextSize(1);
    screen.setTextColor(COLOR_RGB565_LGRAY);
    screen.setCursor(10, 140);
    screen.println("Common durations:");
    screen.setCursor(10, 155);
    screen.println("5, 10, 15, 20, 30, 45, 60 min");
    screen.setCursor(10, 170);
    screen.println("Range: 1-120 minutes");
    
    // Instructions
    screen.setTextSize(1);
    screen.setTextColor(COLOR_RGB565_YELLOW);
    screen.setCursor(10, 200);
    screen.println("Rotate to adjust duration");
    screen.setCursor(10, 215);
    screen.println("Press button to start zone");
    screen.setCursor(10, 230);
    screen.println("Long press to go back");
    
  } else {
    // Zone selection mode using scrollable list
    drawScrollableList(screen, manualRunScrollList, true);
  }
}

void startManualZone(int zoneIdx) {
  DEBUG_PRINTF("=== STARTING MANUAL ZONE %d ===\n", zoneIdx);
  DEBUG_PRINTF("Zone name: %s\n", relayLabels[zoneIdx]);
  DEBUG_PRINTF("Zone pin: %d\n", relayPins[zoneIdx]);

  stopAllActivity(); // Ensure only one operation runs at a time

  // Switch ON the zone
  DEBUG_PRINTF("Activating zone %d relay (pin %d)\n", zoneIdx, relayPins[zoneIdx]);
  relayStates[zoneIdx] = true;
  digitalWrite(relayPins[zoneIdx], HIGH);

  // Switch ON the pump
  DEBUG_PRINTF("Activating pump relay (pin %d)\n", relayPins[PUMP_IDX]);
  relayStates[PUMP_IDX] = true;
  digitalWrite(relayPins[PUMP_IDX], HIGH);

  // Initialize timer variables for manual run with selected duration
  currentRunningZone = zoneIdx;
  zoneStartTime = millis();
  zoneDuration = selectedManualDuration * 60000;  // Convert minutes to milliseconds
  isTimedRun = true;
  currentOperation = OP_MANUAL_ZONE;
  
  // Reset selection state
  selectingDuration = false;

  DEBUG_PRINTF("Zone %d and pump are now ACTIVE\n", zoneIdx);
  DEBUG_PRINTF("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Move to "running zone" state (indefinite or timed, your choice)
  navigateTo(STATE_RUNNING_ZONE);
}

void drawRunningZoneMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Title
  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Zone Running");

  // Show current date/time
  drawDateTimeComponent(screen, 10, 10, currentDateTime, getCurrentDayOfWeek());

  // Calculate elapsed time
  unsigned long elapsed = millis() - zoneStartTime;
  unsigned long elapsedSeconds = elapsed / 1000;
  unsigned long elapsedMinutes = elapsedSeconds / 60;
  unsigned long remainingSeconds = elapsedSeconds % 60;

  // Display running zone information
  screen.setTextSize(2);
  if (currentRunningZone > 0) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setCursor(10, 80);
    screen.printf("Active: %s", relayLabels[currentRunningZone]);
    
    // Show elapsed time
    screen.setCursor(10, 110);
    screen.setTextColor(COLOR_RGB565_CYAN);
    screen.printf("Running: %02lu:%02lu", elapsedMinutes, remainingSeconds);
    
    // Show pump status
    screen.setCursor(10, 140);
    screen.setTextColor(relayStates[PUMP_IDX] ? COLOR_RGB565_GREEN : COLOR_RGB565_RED);
    screen.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    // Show run type
    screen.setTextSize(1);
    screen.setCursor(10, 170);
    screen.setTextColor(COLOR_RGB565_WHITE);
    if (isTimedRun && zoneDuration > 0) {
      unsigned long totalMinutes = zoneDuration / 60000;
      unsigned long remainingTime = (zoneDuration - elapsed) / 1000;
      unsigned long remMinutes = remainingTime / 60;
      unsigned long remSeconds = remainingTime % 60;
      screen.printf("Timed run: %lu min total", totalMinutes);
      screen.setCursor(10, 185);
      screen.setTextColor(COLOR_RGB565_YELLOW);
      screen.printf("Time left: %02lu:%02lu", remMinutes, remSeconds);
    } else {
      screen.println("Manual run (indefinite)");
    }
  } else {
    screen.setTextColor(COLOR_RGB565_RED);
    screen.setCursor(10, 80);
    screen.println("No Zone Active");
  }

  // Show compact zone status
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 210);
  screen.println("All Zones:");

  for (int i = 1; i < NUM_RELAYS; i++) {
    int yPos = 225 + (i-1) * 10;
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
  screen.setCursor(10, 300);
  screen.println("Press button to stop zone");
}

void stopAllActivity() { // Renamed from stopZone
  DEBUG_PRINTLN("=== STOPPING ALL ACTIVITY ===");
  
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
  
  // Reset timer variables for manual run
  currentRunningZone = -1;
  zoneStartTime = 0;
  zoneDuration = 0;
  isTimedRun = false;

  // Reset timer variables for program run
  currentRunningProgram = -1;
  currentProgramZoneIndex = -1;
  programZoneStartTime = 0;
  programInterZoneDelayStartTime = 0;
  inInterZoneDelay = false;
  
  currentOperation = OP_NONE; // No operation is active
  
  DEBUG_PRINTLN("All zones and pump are now OFF. All activity stopped.");
}

// -----------------------------------------------------------------------------
//                       SET SYSTEM TIME (FULLY IMPLEMENTED)
// -----------------------------------------------------------------------------
// Buffers for dynamically generating the menu item strings
char setTimeDisplayStrings[7][32]; // Increased size for "Back"
const char* setTimeDisplayPointers[7];

void drawSetSystemTimeMenu() {
  // Dynamically generate the display strings for the list
  sprintf(setTimeDisplayStrings[0], "Year  : %d", currentDateTime.year);
  sprintf(setTimeDisplayStrings[1], "Month : %d", currentDateTime.month);
  sprintf(setTimeDisplayStrings[2], "Day   : %d", currentDateTime.day);
  sprintf(setTimeDisplayStrings[3], "Hour  : %d", currentDateTime.hour);
  sprintf(setTimeDisplayStrings[4], "Minute: %d", currentDateTime.minute);
  sprintf(setTimeDisplayStrings[5], "Second: %d", currentDateTime.second);
  sprintf(setTimeDisplayStrings[6], "Back to Settings");


  // The scrollable list component expects an array of const char*
  for (int i = 0; i < 7; i++) {
    setTimeDisplayPointers[i] = setTimeDisplayStrings[i];
  }
  setTimeScrollList.items = setTimeDisplayPointers;

  // Highlight the background of the item being edited
  if (editingTimeField) {
    setTimeScrollList.selected_item_bg_color = COLOR_RGB565_ORANGE;
  } else {
    setTimeScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
  }

  drawScrollableList(screen, setTimeScrollList, true);
}

void handleSetSystemTimeEncoder(long diff) {
  if (editingTimeField) {
    // In edit mode, modify the value of the selected field
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
    }
  } else {
    // In selection mode, navigate the list
    handleScrollableListInput(setTimeScrollList, diff);
  }
  
  // Redraw the screen with updated value
  drawSetSystemTimeMenu();
}

void handleSetSystemTimeButton() {
  // If "Back" is selected, just go back
  if (timeEditFieldIndex == setTimeScrollList.num_items) {
    goBack();
    return;
  }

  // Toggle between selection and editing mode for the other fields
  editingTimeField = !editingTimeField;

  // If we just finished editing a field, move to the next one automatically
  if (!editingTimeField) {
    timeEditFieldIndex++;
    if (timeEditFieldIndex >= setTimeScrollList.num_items) {
        timeEditFieldIndex = 0; 
    }
  }
  
  drawSetSystemTimeMenu();
}

// -----------------------------------------------------------------------------
//          PROGRAM A / B / C CONFIG EDIT (ENHANCED)
// -----------------------------------------------------------------------------
void drawProgramConfigMenu(const char* label, ProgramConfig& cfg) {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.print(label);
  screen.println(" Configuration");

  // --- Draw non-scrolling fields ---
  screen.setTextSize(2);
  uint16_t color;

  // Field 0: Enabled
  color = (programEditFieldIndex == 0) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  screen.setTextColor(color);
  screen.setCursor(10, 40);
  screen.printf("Enabled: %s", cfg.enabled ? "YES" : "NO");

  // Field 1 & 2: Start Time
  color = (programEditFieldIndex >= 1 && programEditFieldIndex <= 2) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  screen.setTextColor(color);
  screen.setCursor(10, 65);
  screen.printf("Start Time: %02d:%02d", cfg.startTime.hour, cfg.startTime.minute);

  // Field 3: Inter-Zone Delay
  color = (programEditFieldIndex == 3) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  screen.setTextColor(color);
  screen.setCursor(10, 90);
  screen.printf("Inter-Zone Delay: %d min", cfg.interZoneDelay);

  // Fields 4-10: Days Active
  screen.setTextSize(1);
  screen.setCursor(10, 115);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.println("Days Active:");
  const char* dayLabels[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (int i = 0; i < 7; i++) {
    int xPos = 10 + i * 45;
    color = (programEditFieldIndex == (4 + i)) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(xPos, 130);
    screen.printf("%s %c", dayLabels[i], (cfg.daysActive & (1 << i)) ? '*' : ' ');
  }

  // --- Draw scrollable list for Zone Durations ---
  bool is_zone_list_active = (programEditFieldIndex >= 11);
  drawScrollableList(screen, programZonesScrollList, is_zone_list_active);


  // --- Instructions ---
  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 290);
  screen.println("Rotate to change, Press to select next.");
  screen.println("Long press to exit.");
}

void handleProgramEditEncoder(long diff, ProgramConfig &cfg, const char* progLabel) {
  // New field indices: 0=enabled, 1=hour, 2=minute, 3=interZoneDelay, 4-10=days, 11=zone list
  if (programEditFieldIndex == 11 && editingProgramZone) {
    // We are editing a specific zone's duration
    int selected_zone_index = *programZonesScrollList.selected_index_ptr;
    if (selected_zone_index < programZonesScrollList.num_items) {
      int newDur = cfg.zoneDurations[selected_zone_index] + diff;
      if (newDur < 0)   newDur = 120;
      if (newDur > 120) newDur = 0;
      cfg.zoneDurations[selected_zone_index] = newDur;
    }
  } else if (programEditFieldIndex == 11) {
    // We are navigating the zone list
    handleScrollableListInput(programZonesScrollList, diff);
  }
  else {
    // Handle non-list fields
    switch (programEditFieldIndex) {
      case 0: // Enabled
        if (diff != 0) cfg.enabled = !cfg.enabled;
        break;
      case 1: // Start Hour
        cfg.startTime.hour = (cfg.startTime.hour + diff + 24) % 24;
        break;
      case 2: // Start Minute
        cfg.startTime.minute = (cfg.startTime.minute + diff + 60) % 60;
        break;
      case 3: // Inter-Zone Delay
        cfg.interZoneDelay += diff;
        if (cfg.interZoneDelay < 0)  cfg.interZoneDelay = 30;
        if (cfg.interZoneDelay > 30) cfg.interZoneDelay = 0;
        break;
      case 4: case 5: case 6: case 7: case 8: case 9: case 10: // Days Active
        {
          uint8_t dayBit = (1 << (programEditFieldIndex - 4));
          if (diff != 0) { // Only toggle on movement
            cfg.daysActive ^= dayBit; // Toggle the bit
          }
        }
        break;
    }
  }
  
  drawProgramConfigMenu(progLabel, cfg);
}

void handleProgramEditButton(ProgramConfig &cfg, UIState thisState, const char* progLabel) {
  // If we are in the zone list (field index 11)
  if (programEditFieldIndex == 11) {
    int selected_zone_index = *programZonesScrollList.selected_index_ptr;

    // If the "Back" button is selected, go back.
    if (selected_zone_index == programZonesScrollList.num_items) {
      programEditFieldIndex = 0; // Reset for next time
      editingProgramZone = false;
      goBack();
      return;
    }

    // Toggle editing mode for the selected zone
    editingProgramZone = !editingProgramZone;

    // If we are NOT editing anymore, it means we've confirmed the value.
    // Let's just stay in the list to allow editing another zone.
    // A long press should be used to exit, which is handled by goBack().
    // Or the user can select the "Back" item.

  } else {
    // Otherwise, advance to the next field
    programEditFieldIndex++;
  }
  
  // If we've moved past the simple fields (0-10), we enter the zone list editing mode (11).
  if (programEditFieldIndex > 10) {
    programEditFieldIndex = 11; // Cap at 11
  }
  
  drawProgramConfigMenu(progLabel, cfg);
}

void startProgramRun(int programIndex, ActiveOperationType type) {
  DEBUG_PRINTF("=== STARTING PROGRAM %d (%s) ===\n", programIndex, programs[programIndex]->name);
  stopAllActivity(); // Ensure only one operation runs at a time

  currentRunningProgram = programIndex;
  currentProgramZoneIndex = 0; // Start with the first zone
  programZoneStartTime = millis();
  inInterZoneDelay = false;
  currentOperation = type; // OP_MANUAL_PROGRAM or OP_SCHEDULED_PROGRAM

  DEBUG_PRINTF("Program %s started. Current operation: %d\n", programs[programIndex]->name, currentOperation);
  navigateTo(STATE_PROGRAM_RUNNING);
}

void updateProgramRun() {
  if (currentRunningProgram == -1 || currentOperation == OP_NONE) return;

  ProgramConfig* cfg = programs[currentRunningProgram];
  unsigned long currentTime = millis();

  if (inInterZoneDelay) {
    unsigned long elapsedDelay = currentTime - programInterZoneDelayStartTime;
    if (elapsedDelay >= (unsigned long)cfg->interZoneDelay * 60000) { // Delay in minutes
      inInterZoneDelay = false;
      currentProgramZoneIndex++; // Move to next zone
      programZoneStartTime = currentTime; // Reset timer for new zone
      DEBUG_PRINTF("Inter-zone delay finished. Moving to zone %d\n", currentProgramZoneIndex + 1);
    }
  }

  // Check if current zone needs to be activated or timed out
  if (!inInterZoneDelay) {
    if (currentProgramZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentProgramZoneIndex + 1; // Zones are 1-indexed
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentProgramZoneIndex] * 60000; // Minutes to milliseconds

      // Activate zone and pump if not already active
      if (!relayStates[zoneToRun] || !relayStates[PUMP_IDX]) {
        stopAllActivity(); // Ensure clean state before activating
        DEBUG_PRINTF("Activating program zone %d (%s) and pump\n", zoneToRun, relayLabels[zoneToRun]);
        relayStates[zoneToRun] = true;
        digitalWrite(relayPins[zoneToRun], HIGH);
        relayStates[PUMP_IDX] = true;
        digitalWrite(relayPins[PUMP_IDX], HIGH);
        programZoneStartTime = currentTime; // Ensure timer starts when relays are actually on
      }

      unsigned long elapsedZoneTime = currentTime - programZoneStartTime;
      if (elapsedZoneTime >= zoneRunDuration) {
        DEBUG_PRINTF("Program %s, Zone %d finished. Starting inter-zone delay.\n", cfg->name, zoneToRun);
        // Turn off current zone, but keep pump on for delay if needed
        relayStates[zoneToRun] = false;
        digitalWrite(relayPins[zoneToRun], LOW);
        
        inInterZoneDelay = true;
        programInterZoneDelayStartTime = currentTime;
      }
    } else {
      // All zones in the program are complete
      DEBUG_PRINTF("Program %s completed.\n", cfg->name);
      stopAllActivity();
      navigateTo(STATE_MAIN_MENU);
    }
  }
  // Update display every 5 seconds
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    drawProgramRunningMenu();
  }
}

void drawProgramRunningMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Program Running");

  drawDateTimeComponent(screen, 10, 10, currentDateTime, getCurrentDayOfWeek());

  if (currentRunningProgram != -1) {
    ProgramConfig* cfg = programs[currentRunningProgram];
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setCursor(10, 80);
    screen.printf("Program: %s", cfg->name);

    if (inInterZoneDelay) {
      screen.setTextColor(COLOR_RGB565_CYAN);
      screen.setCursor(10, 110);
      unsigned long elapsedDelay = (millis() - programInterZoneDelayStartTime) / 1000;
      unsigned long remainingDelay = (unsigned long)cfg->interZoneDelay * 60 - elapsedDelay;
      screen.printf("Delay: %02lu:%02lu", remainingDelay / 60, remainingDelay % 60);
      screen.setCursor(10, 140);
      screen.setTextColor(COLOR_RGB565_WHITE);
      screen.println("Waiting for next zone...");
    } else if (currentProgramZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentProgramZoneIndex + 1;
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentProgramZoneIndex] * 60000;
      unsigned long elapsedZoneTime = millis() - programZoneStartTime;
      unsigned long remainingZoneTime = (zoneRunDuration - elapsedZoneTime) / 1000;

      screen.setTextColor(COLOR_RGB565_CYAN);
      screen.setCursor(10, 110);
      screen.printf("Zone %d: %s", zoneToRun, relayLabels[zoneToRun]);
      screen.setCursor(10, 140);
      screen.printf("Time left: %02lu:%02lu", remainingZoneTime / 60, remainingZoneTime % 60);
    } else {
      screen.setTextColor(COLOR_RGB565_GREEN);
      screen.setCursor(10, 110);
      screen.println("Program Finishing...");
    }

    screen.setCursor(10, 170);
    screen.setTextColor(relayStates[PUMP_IDX] ? COLOR_RGB565_GREEN : COLOR_RGB565_RED);
    screen.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    screen.setTextSize(1);
    screen.setTextColor(COLOR_RGB565_WHITE);
    screen.setCursor(10, 200);
    screen.println("Current Status:");
    for (int i = 1; i < NUM_RELAYS; i++) {
      int yPos = 215 + (i-1) * 10;
      screen.setCursor(10, yPos);
      if (relayStates[i]) {
        screen.setTextColor(COLOR_RGB565_GREEN);
      } else {
        screen.setTextColor(COLOR_RGB565_LGRAY);
      }
      screen.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
    }
  } else {
    screen.setTextColor(COLOR_RGB565_RED);
    screen.setCursor(10, 80);
    screen.println("No Program Active");
  }

  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 300);
  screen.println("Press button to stop program");
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
  // This function is now simpler. It just draws the status and the list.
  // The main screen clearing is handled in enterState.
  
  // --- Draw the static status information at the top ---
  // We only need to redraw this part if something changes, but for simplicity,
  // we'll redraw it each time. A more optimized approach would be to have a
  // separate function that only updates the top status bar.
  screen.fillRect(0, 0, 320, 50, COLOR_RGB565_BLACK); // Clear top area
  screen.setTextSize(1);
  screen.setCursor(10, 10);
  if (wifiConnected) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.printf("WiFi: Connected (%s)", WiFi.SSID().c_str());
  } else {
    screen.setTextColor(COLOR_RGB565_RED);
    screen.println("WiFi: Not Connected");
  }

  screen.setCursor(10, 25);
  if (timeSync) {
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.println("Time: NTP Synced");
  } else {
    screen.setTextColor(COLOR_RGB565_YELLOW);
    screen.println("Time: Manual/Software Clock");
  }
  
  // Draw a separator line
  screen.drawLine(0, 45, 320, 45, COLOR_RGB565_LGRAY);

  // --- Draw the scrollable list for the settings items ---
  // The list is drawn starting from y=50, as configured in enterState
  drawScrollableList(screen, settingsMenuScrollList, true);
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
  navigateTo(STATE_SETTINGS);
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
  navigateTo(STATE_SETTINGS);
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
  
  navigateTo(STATE_SETTINGS);
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
  
  stopAllActivity(); // Ensure no other activity is running
  
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
      navigateTo(STATE_MAIN_MENU);
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
  
  stopAllActivity(); // Ensure all relays are off
  
  DEBUG_PRINTLN("Test mode stopped - all relays OFF");
}
