#include <Arduino.h>
#include "st7789_dma_driver.h" // Replaced DFRobot_GDL.h
#include "styling.h"
#include "CustomCanvas.h"
#include "ui_components.h"
#include "web_server.h" // Include the web server header
#include "wifi_manager.h" // Include the new WiFi manager
#include "config_manager.h" // Include the configuration manager
#include "logo.h"
#include <LittleFS.h>

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
volatile bool encoder_button_pressed = false;

// For button software debounce:
unsigned long lastButtonPressTime = 0;
const unsigned long buttonDebounce = 200; // milliseconds

// -----------------------------------------------------------------------------
//                        Relay Pins / Configuration
// -----------------------------------------------------------------------------
const int NUM_RELAYS = 8; // Made non-static for web_server.h extern
// Relay 0 is dedicated to the borehole pump;
// Relays 1..7 are the irrigation zones.
static const int relayPins[NUM_RELAYS] = {19, 20, 17, 18, 15, 21, 1, 14}; 
bool relayStates[NUM_RELAYS] = {false, false, false, false, false, false, false, false};

static const int PUMP_IDX = 0;   // borehole pump
// ZONE_COUNT is now defined in ui_components.h

// -----------------------------------------------------------------------------
//                           Display Pins / Driver
// -----------------------------------------------------------------------------
#define TFT_DC   2
#define TFT_CS   6
#define TFT_RST  3
#define TFT_BL   5 // Backlight control pin for dimming

CustomCanvas canvas(320, 240);

// Relay labels (index 0 is the pump)
const char* relayLabels[NUM_RELAYS] = {
  "Pump (auto)", // index 0; not displayed in manual-run menu
};

// -----------------------------------------------------------------------------
//                           Screen Dimming
// -----------------------------------------------------------------------------
unsigned long lastActivityTime = 0;
const unsigned long inactivityTimeout = 30000; // 30 seconds
bool isScreenDimmed = false;
volatile bool uiDirty = true; // Flag to trigger UI redraw

// -----------------------------------------------------------------------------
//                        Current Sensing Pin / Configuration
// -----------------------------------------------------------------------------
// const int CurrentSensorPin = 8; 


// -----------------------------------------------------------------------------
//                           Menu and Cycle States
// -----------------------------------------------------------------------------
// ActiveOperationType is now defined in ui_components.h

enum UIState {
  STATE_BOOTING,
  STATE_MAIN_MENU,
  STATE_MANUAL_RUN,
  STATE_CYCLES_MENU,
  STATE_CYCLE_A_MENU,
  STATE_CYCLE_B_MENU,
  STATE_CYCLE_C_MENU,
  STATE_SETTINGS,
  STATE_SET_SYSTEM_TIME,
  STATE_WIFI_SETUP_LAUNCHER, // Renamed from STATE_WIFI_SETUP
  STATE_WIFI_RESET,
  STATE_SYSTEM_INFO,
  STATE_PROG_A,
  STATE_PROG_B,
  STATE_PROG_C,
  STATE_RUNNING_ZONE,
  STATE_CYCLE_RUNNING,
  STATE_TEST_MODE
};

UIState currentState = STATE_BOOTING;
const int UI_STATE_STACK_SIZE = 10;
UIState uiStateStack[UI_STATE_STACK_SIZE];
int uiStateStackPtr = -1;
ActiveOperationType currentOperation = OP_NONE; // Track what kind of operation is active

// Main Menu Items
static const int MAIN_MENU_ITEMS = 4; 
const char* mainMenuLabels[MAIN_MENU_ITEMS] = {
  "Manual Run",
  "Cycles", // New submenu
  "Test Mode",
  "Settings"
};
int selectedMainMenuIndex = 0; 

// Cycles Menu Items (lists Cycle A, B, C)
static const int CYCLES_MENU_ITEMS = 3; // A, B, C
const char* cyclesMenuLabels[CYCLES_MENU_ITEMS] = {
  "Cycle A",
  "Cycle B",
  "Cycle C"
};
int selectedCyclesMenuIndex = 0;

// Individual Cycle Sub-Menu Items (Run Now, Configure)
static const int CYCLE_SUB_MENU_ITEMS = 2; // Run Now, Configure
const char* cycleSubMenuLabels[CYCLE_SUB_MENU_ITEMS] = {
  "Run Now",
  "Configure"
};
int selectedCycleSubMenuIndex = 0;
ScrollableList cycleSubMenuScrollList;

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

// WiFi Setup Launcher Menu Items
static const int WIFI_SETUP_LAUNCHER_MENU_ITEMS = 2;
const char* wifiSetupLauncherMenuLabels[WIFI_SETUP_LAUNCHER_MENU_ITEMS] = {
  "Start WiFi Portal",
  "Back"
};
int selectedWiFiSetupLauncherIndex = 0;
ScrollableList wifiSetupLauncherScrollList;

// Manual Run zone index: 0..6 => zone = index+1
int selectedManualZoneIndex = 0;
// Made non-static to be accessible from web_server.cpp
int selectedManualDuration = 5;  // Default 5 minutes 
bool selectingDuration = false;  // Whether we're selecting duration or zone
ScrollableList manualRunScrollList;
const char* zoneNamePointers[ZONE_COUNT]; // Array of pointers for scrollable list

// Cycle Config zone selection
char cycleZoneDisplayStrings[ZONE_COUNT][50];
const char* cycleZoneDisplayPointers[ZONE_COUNT];
int selectedCycleZoneIndex = 0; // Used by all program config screens
bool editingCycleZone = false; // Are we editing a zone duration?

// -----------------------------------------------------------------------------
//                           Zone Timer Variables
// -----------------------------------------------------------------------------
unsigned long zoneStartTime = 0;        // When current zone started
unsigned long zoneDuration = 0;         // Duration for current zone in milliseconds
int currentRunningZone = -1;            // Which zone is currently running (-1 = none)
bool isTimedRun = false;                 // Whether this is a timed run or manual indefinite run
int currentRunningCycle = -1;         // Which cycle is currently running (-1 = none, 0=A, 1=B, 2=C)
int currentCycleZoneIndex = -1;       // Current zone index within a running cycle
unsigned long cycleZoneStartTime = 0; // When current cycle zone started
unsigned long cycleInterZoneDelayStartTime = 0; // When inter-zone delay started
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
//                 Cycle Config
// -----------------------------------------------------------------------------
// TimeOfDay and CycleConfig structs are now defined in ui_components.h

CycleConfig* cycles[] = {&systemConfig.cycles[0], &systemConfig.cycles[1], &systemConfig.cycles[2]};
const int NUM_CYCLES = 3; // Made non-static for web_server.h extern

// -----------------------------------------------------------------------------
//                Sub-indexes and helpers for editing fields
// -----------------------------------------------------------------------------
static int timeEditFieldIndex = 0;    // 0=year,1=month,2=day,3=hour,4=minute,5=second
static bool editingTimeField = false;
static int cycleEditFieldIndex = 0; // 0=enabled, 1=hour, 2=minute, 3=interZoneDelay, 4-10=days, 11-17=zone durations
static int zoneEditScrollOffset = 0; // For scrolling through zones in cycle config

// Ranges for system time fields (simple example)
#define MIN_YEAR  2020
#define MAX_YEAR  2050


// Global instance for main menu
ScrollableList mainMenuScrollList;
ScrollableList cyclesMenuScrollList;
ScrollableList cycleZonesScrollList; // For zone durations in cycle config
ScrollableList setTimeScrollList;

// -----------------------------------------------------------------------------
//                           Forward Declarations
// -----------------------------------------------------------------------------
void updateScreen();
void render();
void isrPinA();
void handleEncoderMovement();
void handleButtonPress();

void drawLogo();
void drawMainMenu();
void drawCyclesMenu();
void drawCycleSubMenu(const char* label);
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
void drawCycleConfigMenu(const char* label, CycleConfig& cfg);
void handleCycleEditEncoder(long diff, CycleConfig &cfg, const char* progLabel);
void handleCycleEditButton(CycleConfig &cfg, UIState thisState, const char* progLabel);
void startCycleRun(int cycleIndex, ActiveOperationType type);
void updateCycleRun();
void drawCycleRunningMenu();

// Settings menu functions
void drawSettingsMenu();
void drawWiFiResetMenu();
void drawSystemInfoMenu();
void drawWiFiSetupLauncherMenu(); // Renamed from drawWiFiSetupMenu

// Test Mode functions
void startTestMode();
void updateTestMode();
void drawTestModeMenu();
void stopTestMode();



// -----------------------------------------------------------------------------
//                                     SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000); // Allow serial to initialize
  DEBUG_PRINTLN("=== IRRIGATION CONTROLLER STARTUP ===");
  DEBUG_PRINTF("Firmware Version: v1.0\n");
  DEBUG_PRINTF("Hardware: ESP32-C6\n");
  DEBUG_PRINTF("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Initialize display
  DEBUG_PRINTLN("Initializing display with custom driver...");
  st7789_init_display(TFT_DC, TFT_CS, TFT_RST, TFT_BL, &SPI); 
  canvas.fillScreen(COLOR_BACKGROUND); // Clear canvas
  DEBUG_PRINTLN("Display initialized successfully with custom driver");

  // Backlight is handled by st7789_init_display and st7789_set_backlight
  lastActivityTime = millis();

  // Show logo on boot
  canvas.fillScreen(COLOR_BACKGROUND);
  int16_t logo_x = (320 - LOGO_WIDTH) / 2;
  int16_t logo_y = (240 - LOGO_HEIGHT) / 2;
  canvas.drawRGBBitmap(logo_x, logo_y, logo_data, LOGO_WIDTH, LOGO_HEIGHT);
  updateScreen();
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

  // Initialize WiFi Manager. It will start connecting if credentials are saved.
  wifi_manager_init();

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount file system");
    // Handle error appropriately, maybe by rebooting or halting
  }

  // Load configuration from LittleFS
  if (!loadConfig()) {
    // If config fails to load, save the defaults
    saveConfig();
  }

  // Initialize the Web Server
  initWebServer();

  // Move to appropriate state
  navigateTo(STATE_MAIN_MENU);
  DEBUG_PRINTLN("=== STARTUP COMPLETE ===");
}

// -----------------------------------------------------------------------------
//                                RENDER
// -----------------------------------------------------------------------------
void render() {
  if (isScreenDimmed) {
    if (uiDirty) {
      uiDirty = false;
    }
    return;
  }

  if (!uiDirty) return;

  // Draw the current UI state to the canvas
  switch (currentState) {
    case STATE_MAIN_MENU:       drawMainMenu(); break;
    case STATE_CYCLES_MENU:     drawCyclesMenu(); break;
    case STATE_CYCLE_A_MENU:    drawCycleSubMenu("Cycle A"); break;
    case STATE_CYCLE_B_MENU:    drawCycleSubMenu("Cycle B"); break;
    case STATE_CYCLE_C_MENU:    drawCycleSubMenu("Cycle C"); break;
    case STATE_MANUAL_RUN:      drawManualRunMenu(); break;
    case STATE_SETTINGS:        drawSettingsMenu(); break;
    case STATE_SET_SYSTEM_TIME: drawSetSystemTimeMenu(); break;
    case STATE_PROG_A:          drawCycleConfigMenu("Cycle A", systemConfig.cycles[0]); break;
    case STATE_PROG_B:          drawCycleConfigMenu("Cycle B", systemConfig.cycles[1]); break;
    case STATE_PROG_C:          drawCycleConfigMenu("Cycle C", systemConfig.cycles[2]); break;
    case STATE_RUNNING_ZONE:    drawRunningZoneMenu(); break;
    case STATE_CYCLE_RUNNING:   drawCycleRunningMenu(); break;
    case STATE_TEST_MODE:       drawTestModeMenu(); break;
    case STATE_WIFI_SETUP_LAUNCHER: drawWiFiSetupLauncherMenu(); break;
    case STATE_WIFI_RESET:      drawWiFiResetMenu(); break;
    case STATE_SYSTEM_INFO:     drawSystemInfoMenu(); break;
    default:
      // Draw an error screen or something
      canvas.fillScreen(COLOR_ERROR);
      canvas.setCursor(LEFT_PADDING, 10);
      canvas.setTextSize(2);
      canvas.setTextColor(COLOR_TEXT_PRIMARY);
      canvas.println("Unknown UI State!");
      break;
  }

  // Push the canvas to the screen
  updateScreen();
  
  uiDirty = false; // Reset the flag
}

// -----------------------------------------------------------------------------
//                                     LOOP
// -----------------------------------------------------------------------------
void loop() {
  // Handle WiFi and NTP updates
  wifi_manager_handle();
  
  if (encoder_button_pressed && currentState == STATE_BOOTING) {
    wifi_manager_cancel_connection();
    encoder_button_pressed = false; // Reset flag
  }

  if (wifi_manager_is_connecting() == false && currentState == STATE_BOOTING) {
    navigateTo(STATE_MAIN_MENU);
  }

  // Update time - use NTP if available, otherwise software clock
  if (wifi_manager_is_time_synced()) {
    wifi_manager_update_system_time(currentDateTime); // Update our time structure from system time
  } else {
    updateSoftwareClock(); // Fallback to software clock
  }
  
  handleEncoderMovement();
  handleButtonPress();

  // Render the UI if needed
  render();

  // Handle screen dimming
  if (!isScreenDimmed && (millis() - lastActivityTime > inactivityTimeout)) {
    isScreenDimmed = true;
    st7789_set_backlight(false); // Dim the screen using the driver
    DEBUG_PRINTLN("Screen dimmed due to inactivity.");
  }

  // Check for scheduled cycles if no other operation is active
  if (currentOperation == OP_NONE) {
    DayOfWeek currentDay = getCurrentDayOfWeek();
    for (int i = 0; i < NUM_CYCLES; i++) {
      CycleConfig* cfg = cycles[i];
      if (cfg->enabled && (cfg->daysActive & currentDay) &&
          currentDateTime.hour == cfg->startTime.hour &&
          currentDateTime.minute == cfg->startTime.minute &&
          currentDateTime.second == 0) { // Trigger at the start of the minute
        DEBUG_PRINTF("Scheduled cycle %s triggered!\n", cfg->name);
        startCycleRun(i, OP_SCHEDULED_CYCLE);
        break; // Only one cycle can start per second
      }
    }
  }

  // Additional logic for running states if needed
  if (currentState == STATE_RUNNING_ZONE) {
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 5000) {
      lastDisplayUpdate = millis();
      uiDirty = true;
    }
    
    if (isTimedRun && zoneDuration > 0) {
      unsigned long elapsed = millis() - zoneStartTime;
      if (elapsed >= zoneDuration) {
        DEBUG_PRINTLN("Zone timer expired - stopping zone");
        stopAllActivity();
        navigateTo(STATE_MAIN_MENU);
      }
    }
  } else if (currentState == STATE_CYCLE_RUNNING) {
    updateCycleRun();
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
  uiDirty = true;
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

  lastActivityTime = millis();
  if (isScreenDimmed) {
    st7789_set_backlight(true); // Full brightness
    isScreenDimmed = false;
    uiDirty = true; // Redraw the screen
    DEBUG_PRINTLN("Screen woken up by encoder movement.");
    return; // Ignore the first input
  }
  uiDirty = true;

  DEBUG_PRINTF("Encoder moved: diff=%ld, state=%d\n", diff, currentState);

  switch (currentState) {
    case STATE_MAIN_MENU:
      handleScrollableListInput(mainMenuScrollList, diff);
      break;

    case STATE_CYCLES_MENU:
      handleScrollableListInput(cyclesMenuScrollList, diff);
      break;

    case STATE_CYCLE_A_MENU:
    case STATE_CYCLE_B_MENU:
    case STATE_CYCLE_C_MENU:
      handleScrollableListInput(cycleSubMenuScrollList, diff);
      break;

    case STATE_MANUAL_RUN:
      if (selectingDuration) {
        selectedManualDuration += diff;
        if (selectedManualDuration < 1) selectedManualDuration = 120;
        if (selectedManualDuration > 120) selectedManualDuration = 1;
        DEBUG_PRINTF("Manual run duration selection: %d minutes\n", selectedManualDuration);
      } else {
        handleScrollableListInput(manualRunScrollList, diff);
      }
      break;

    case STATE_SETTINGS:
      handleScrollableListInput(settingsMenuScrollList, diff);
      break;

    case STATE_WIFI_SETUP_LAUNCHER:
      handleScrollableListInput(wifiSetupLauncherScrollList, diff);
      break;

    case STATE_SET_SYSTEM_TIME:
      handleSetSystemTimeEncoder(diff);
      break;

    case STATE_PROG_A:
      handleCycleEditEncoder(diff, systemConfig.cycles[0], "Cycle A");
      break;

    case STATE_PROG_B:
      handleCycleEditEncoder(diff, systemConfig.cycles[1], "Cycle B");
      break;

    case STATE_PROG_C:
      handleCycleEditEncoder(diff, systemConfig.cycles[2], "Cycle C");
      break;

    case STATE_RUNNING_ZONE:
    case STATE_CYCLE_RUNNING:
      DEBUG_PRINTLN("Encoder ignored - zone/cycle is running");
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
      encoder_button_pressed = true;

      lastActivityTime = millis();
      if (isScreenDimmed) {
        st7789_set_backlight(true); // Full brightness
        isScreenDimmed = false;
        uiDirty = true; // Redraw the screen
        DEBUG_PRINTLN("Screen woken up by button press.");
        return; // Ignore the first input
      }
      uiDirty = true;

      // State-Specific Handling
      switch (currentState) {
        case STATE_MAIN_MENU:
          switch (selectedMainMenuIndex) {
            case 0: navigateTo(STATE_MANUAL_RUN); break;
            case 1: navigateTo(STATE_CYCLES_MENU); break;
            case 2: navigateTo(STATE_TEST_MODE); break;
            case 3: navigateTo(STATE_SETTINGS); break;
          }
          break;

        case STATE_CYCLES_MENU:
          if (selectedCyclesMenuIndex == CYCLES_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedCyclesMenuIndex) {
              case 0: navigateTo(STATE_CYCLE_A_MENU); break;
              case 1: navigateTo(STATE_CYCLE_B_MENU); break;
              case 2: navigateTo(STATE_CYCLE_C_MENU); break;
            }
          }
          break;

        case STATE_CYCLE_A_MENU:
          if (selectedCycleSubMenuIndex == CYCLE_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedCycleSubMenuIndex) {
              case 0: startCycleRun(0, OP_MANUAL_CYCLE); break;
              case 1: navigateTo(STATE_PROG_A); break;
            }
          }
          break;

        case STATE_CYCLE_B_MENU:
          if (selectedCycleSubMenuIndex == CYCLE_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedCycleSubMenuIndex) {
              case 0: startCycleRun(1, OP_MANUAL_CYCLE); break;
              case 1: navigateTo(STATE_PROG_B); break;
            }
          }
          break;

        case STATE_CYCLE_C_MENU:
          if (selectedCycleSubMenuIndex == CYCLE_SUB_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedCycleSubMenuIndex) {
              case 0: startCycleRun(2, OP_MANUAL_CYCLE); break;
              case 1: navigateTo(STATE_PROG_C); break;
            }
          }
          break;

        case STATE_SETTINGS:
          if (selectedSettingsMenuIndex == SETTINGS_MENU_ITEMS) { // Back button
            goBack();
          } else {
            switch (selectedSettingsMenuIndex) {
              case 0: navigateTo(STATE_WIFI_SETUP_LAUNCHER); break;
              case 1: navigateTo(STATE_SET_SYSTEM_TIME); break;
              case 2: navigateTo(STATE_WIFI_RESET); break;
              case 3: navigateTo(STATE_SYSTEM_INFO); break;
            }
          }
          break;
        
        case STATE_WIFI_SETUP_LAUNCHER:
          if (selectedWiFiSetupLauncherIndex == 0) { // "Start WiFi Portal"
            wifi_manager_start_portal();
            navigateTo(STATE_SETTINGS);
          } else { // "Back"
            goBack();
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
          }
          break;

        case STATE_SET_SYSTEM_TIME:
          DEBUG_PRINTF("System time button - field %d\n", timeEditFieldIndex);
          handleSetSystemTimeButton();
          break;

        case STATE_PROG_A:
          DEBUG_PRINTF("Cycle A button - field %d\n", cycleEditFieldIndex);
          handleCycleEditButton(systemConfig.cycles[0], STATE_PROG_A, "Cycle A");
          break;

        case STATE_PROG_B:
          DEBUG_PRINTF("Cycle B button - field %d\n", cycleEditFieldIndex);
          handleCycleEditButton(systemConfig.cycles[1], STATE_PROG_B, "Cycle B");
          break;

        case STATE_PROG_C:
          DEBUG_PRINTF("Cycle C button - field %d\n", cycleEditFieldIndex);
          handleCycleEditButton(systemConfig.cycles[2], STATE_PROG_C, "Cycle C");
          break;

        case STATE_RUNNING_ZONE:
        case STATE_CYCLE_RUNNING:
          DEBUG_PRINTLN("Cancelling running zone/cycle");
          stopAllActivity();
          navigateTo(STATE_MAIN_MENU);
          break;

        case STATE_TEST_MODE:
          DEBUG_PRINTLN("Cancelling test mode");
          stopTestMode();
          navigateTo(STATE_MAIN_MENU);
          break;

        case STATE_SYSTEM_INFO:
          DEBUG_PRINTLN("Exiting system info screen");
          goBack();
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
    "MAIN_MENU", "MANUAL_RUN", "CYCLES_MENU", "CYCLE_A_MENU", "CYCLE_B_MENU", "CYCLE_C_MENU",
    "SETTINGS", "SET_SYSTEM_TIME", "WIFI_SETUP_LAUNCHER", "WIFI_RESET", "SYSTEM_INFO",
    "PROG_A", "PROG_B", "PROG_C", "RUNNING_ZONE", "CYCLE_RUNNING", "TEST_MODE"
  };
  
  DEBUG_PRINTF("State transition: %s -> %s\n", 
    (currentState < sizeof(stateNames)/sizeof(stateNames[0])) ? stateNames[currentState] : "UNKNOWN",
    (newState < sizeof(stateNames)/sizeof(stateNames[0])) ? stateNames[newState] : "UNKNOWN");
  
  if (!isNavigatingBack) {
    if (currentState != STATE_CYCLE_RUNNING && currentState != STATE_RUNNING_ZONE && currentState != STATE_TEST_MODE) {
      if (uiStateStackPtr < UI_STATE_STACK_SIZE - 1) {
        uiStateStack[++uiStateStackPtr] = currentState;
      } else {
        DEBUG_PRINTLN("UI state stack overflow!");
      }
    }
  }
  
  currentState = newState;
  uiDirty = true;

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
      mainMenuScrollList.title = "Main Menu";
      mainMenuScrollList.title_text_size = 2;
      mainMenuScrollList.show_back_button = false; // No back button on main menu
      setupScrollableListMetrics(mainMenuScrollList, canvas);
      break;
    case STATE_MANUAL_RUN:
      selectedManualZoneIndex = 0;
      selectingDuration = false;
      
      // Populate the pointers array for the scrollable list
      for (int i = 0; i < ZONE_COUNT; i++) {
        zoneNamePointers[i] = systemConfig.zoneNames[i];
      }
      manualRunScrollList.items = zoneNamePointers;
      
      manualRunScrollList.num_items = ZONE_COUNT;
      manualRunScrollList.selected_index_ptr = &selectedManualZoneIndex;
      manualRunScrollList.x = 0;
      manualRunScrollList.y = 0;
      manualRunScrollList.width = 320;
      manualRunScrollList.height = 240;
      manualRunScrollList.item_text_size = 2;
      manualRunScrollList.title = "Select Zone";
      manualRunScrollList.title_text_size = 2;
      manualRunScrollList.show_back_button = true;
      setupScrollableListMetrics(manualRunScrollList, canvas);
      break;
    case STATE_CYCLES_MENU:
      selectedCyclesMenuIndex = 0;
      cyclesMenuScrollList.items = cyclesMenuLabels;
      cyclesMenuScrollList.num_items = CYCLES_MENU_ITEMS;
      cyclesMenuScrollList.selected_index_ptr = &selectedCyclesMenuIndex;
      cyclesMenuScrollList.x = 0;
      cyclesMenuScrollList.y = 70;
      cyclesMenuScrollList.width = 320;
      cyclesMenuScrollList.height = 240 - 70;
      cyclesMenuScrollList.item_text_size = 2;
      cyclesMenuScrollList.title = "Cycles";
      cyclesMenuScrollList.title_text_size = 2;
      cyclesMenuScrollList.show_back_button = true;
      setupScrollableListMetrics(cyclesMenuScrollList, canvas);
      break;
    case STATE_CYCLE_A_MENU:
    case STATE_CYCLE_B_MENU:
    case STATE_CYCLE_C_MENU:
      selectedCycleSubMenuIndex = 0;
      {
        const char* progLabel;
        if (newState == STATE_CYCLE_A_MENU) progLabel = "Cycle A";
        else if (newState == STATE_CYCLE_B_MENU) progLabel = "Cycle B";
        else progLabel = "Cycle C";

        cycleSubMenuScrollList.items = cycleSubMenuLabels;
        cycleSubMenuScrollList.num_items = CYCLE_SUB_MENU_ITEMS;
        cycleSubMenuScrollList.selected_index_ptr = &selectedCycleSubMenuIndex;
        cycleSubMenuScrollList.x = 0;
        cycleSubMenuScrollList.y = 70;
        cycleSubMenuScrollList.width = 320;
        cycleSubMenuScrollList.height = 240 - 70;
        cycleSubMenuScrollList.item_text_size = 2;
        cycleSubMenuScrollList.title = progLabel;
        cycleSubMenuScrollList.title_text_size = 2;
        cycleSubMenuScrollList.show_back_button = true;
        setupScrollableListMetrics(cycleSubMenuScrollList, canvas);
        DEBUG_PRINTF("Entering %s sub-menu\n", progLabel);
      }
      break;
    case STATE_SETTINGS:
      selectedSettingsMenuIndex = 0;
      settingsMenuScrollList.items = settingsMenuLabels;
      settingsMenuScrollList.num_items = SETTINGS_MENU_ITEMS;
      settingsMenuScrollList.selected_index_ptr = &selectedSettingsMenuIndex;
      settingsMenuScrollList.x = 0;
      settingsMenuScrollList.y = 75; 
      settingsMenuScrollList.width = 320;
      settingsMenuScrollList.height = 240 - settingsMenuScrollList.y;
      settingsMenuScrollList.item_text_size = 2;
      settingsMenuScrollList.title = "Settings";
      settingsMenuScrollList.title_text_size = 2;
      settingsMenuScrollList.show_back_button = true;
      setupScrollableListMetrics(settingsMenuScrollList, canvas);
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
      setTimeScrollList.title = "Set System Time";
      setTimeScrollList.title_text_size = 2;
      setTimeScrollList.show_back_button = true;
      setupScrollableListMetrics(setTimeScrollList, canvas);
      break;
    case STATE_WIFI_SETUP_LAUNCHER:
      selectedWiFiSetupLauncherIndex = 0;
      wifiSetupLauncherScrollList.items = wifiSetupLauncherMenuLabels;
      wifiSetupLauncherScrollList.num_items = WIFI_SETUP_LAUNCHER_MENU_ITEMS;
      wifiSetupLauncherScrollList.selected_index_ptr = &selectedWiFiSetupLauncherIndex;
      wifiSetupLauncherScrollList.x = 0;
      wifiSetupLauncherScrollList.y = 70;
      wifiSetupLauncherScrollList.width = 320;
      wifiSetupLauncherScrollList.height = 240 - 70;
      wifiSetupLauncherScrollList.item_text_size = 2;
      wifiSetupLauncherScrollList.title = "WiFi Setup";
      wifiSetupLauncherScrollList.title_text_size = 2;
      wifiSetupLauncherScrollList.show_back_button = false; // "Back" is an explicit item
      setupScrollableListMetrics(wifiSetupLauncherScrollList, canvas);
      DEBUG_PRINTLN("Entering WiFi Setup Launcher");
      break;
    case STATE_WIFI_RESET:
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
        cycleEditFieldIndex = 0;
        editingCycleZone = false;
        selectedCycleZoneIndex = 0;

        CycleConfig* currentProg;
        const char* progLabel;
        if (newState == STATE_PROG_A) { currentProg = &systemConfig.cycles[0]; progLabel = "Cycle A"; }
        else if (newState == STATE_PROG_B) { currentProg = &systemConfig.cycles[1]; progLabel = "Cycle B"; }
        else { currentProg = &systemConfig.cycles[2]; progLabel = "Cycle C"; }

        // Populate pointers for the list
        for (int i = 0; i < ZONE_COUNT; i++) {
            cycleZoneDisplayPointers[i] = cycleZoneDisplayStrings[i];
        }
        cycleZonesScrollList.items = cycleZoneDisplayPointers;
        cycleZonesScrollList.data_source = nullptr; // Not using data_source anymore
        cycleZonesScrollList.format_string = nullptr; // Not using format_string anymore

        cycleZonesScrollList.num_items = ZONE_COUNT;
        cycleZonesScrollList.selected_index_ptr = &selectedCycleZoneIndex;
        cycleZonesScrollList.x = 0;
        cycleZonesScrollList.y = 150;
        cycleZonesScrollList.width = 320;
        cycleZonesScrollList.height = 95;
        cycleZonesScrollList.item_text_size = 2;
        cycleZonesScrollList.title = "Zone Durations (min)";
        cycleZonesScrollList.title_text_size = 2;
        cycleZonesScrollList.show_back_button = true;
        
        setupScrollableListMetrics(cycleZonesScrollList, canvas);
        
        DEBUG_PRINTF("Entering %s configuration\n", progLabel);
      }
      break;
    case STATE_RUNNING_ZONE:
      DEBUG_PRINTLN("Entering running zone state");
      break;
    case STATE_CYCLE_RUNNING:
      DEBUG_PRINTLN("Entering cycle running state");
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
  canvas.fillScreen(COLOR_BACKGROUND);
  drawDateTimeComponent(canvas, LEFT_PADDING, 10, currentDateTime, getCurrentDayOfWeek());
  drawScrollableList(canvas, mainMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           CYCLES MENU DRAWING
// -----------------------------------------------------------------------------
void drawCyclesMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  drawScrollableList(canvas, cyclesMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           CYCLE SUB-MENU DRAWING
// -----------------------------------------------------------------------------
void drawCycleSubMenu(const char* label) {
  canvas.fillScreen(COLOR_BACKGROUND);
  cycleSubMenuScrollList.title = label;
  drawScrollableList(canvas, cycleSubMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           LOGO DISPLAY
// -----------------------------------------------------------------------------

void updateScreen() {
  st7789_push_canvas(canvas.getBuffer(), 320, 240);
}
// -----------------------------------------------------------------------------
//                         SIMPLE SOFTWARE CLOCK
// -----------------------------------------------------------------------------
void updateSoftwareClock() {
  unsigned long now = millis();
  if ((now - lastSecondUpdate) >= 1000) {
    lastSecondUpdate = now;
    incrementOneSecond();

    if (currentState == STATE_MAIN_MENU || currentState == STATE_CYCLE_RUNNING || currentState == STATE_RUNNING_ZONE) {
      uiDirty = true;
    }
  }
}

DayOfWeek getCurrentDayOfWeek() {
  if (wifi_manager_is_time_synced()) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return (DayOfWeek)(1 << timeinfo.tm_wday);
  } else {
    int y = currentDateTime.year;
    int m = currentDateTime.month;
    int d = currentDateTime.day;
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
      y -= 1;
    }
    int dayOfWeekIndex = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return (DayOfWeek)(1 << dayOfWeekIndex);
  }
}

// -----------------------------------------------------------------------------
//                           MANUAL RUN FUNCTIONS
// -----------------------------------------------------------------------------
void drawManualRunMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);

  if (selectingDuration) {
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_ACCENT_SECONDARY);
    canvas.setCursor(LEFT_PADDING, 10);
    canvas.println("Manual Run");
    
    canvas.setCursor(LEFT_PADDING, 40);
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.printf("Zone: %s", systemConfig.zoneNames[selectedManualZoneIndex]);
    
    canvas.setCursor(LEFT_PADDING, 70);
    canvas.setTextColor(COLOR_ACCENT_PRIMARY);
    canvas.println("Select Duration:");
    
    canvas.setCursor(LEFT_PADDING, 100);
    canvas.setTextSize(3);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.printf("%d minutes", selectedManualDuration);
    
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_SECONDARY);
    canvas.setCursor(LEFT_PADDING, 140);
    canvas.println("Common durations:");
    canvas.setCursor(LEFT_PADDING, 155);
    canvas.println("5, 10, 15, 20, 30, 45, 60 min");
    canvas.setCursor(LEFT_PADDING, 170);
    canvas.println("Range: 1-120 minutes");
    
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_ACCENT_SECONDARY);
    canvas.setCursor(LEFT_PADDING, 200);
    canvas.println("Rotate to adjust duration");
    canvas.setCursor(LEFT_PADDING, 215);
    canvas.println("Press button to start zone");
    canvas.setCursor(LEFT_PADDING, 230);
    canvas.println("Long press to go back");
    
  } else {
    drawScrollableList(canvas, manualRunScrollList, true);
  }
}

void startManualZone(int zoneIdx) {
  DEBUG_PRINTF("=== STARTING MANUAL ZONE %d ===\n", zoneIdx);
  DEBUG_PRINTF("Zone name: %s\n", systemConfig.zoneNames[zoneIdx-1]);
  DEBUG_PRINTF("Zone pin: %d\n", relayPins[zoneIdx]);

  stopAllActivity();

  DEBUG_PRINTF("Activating zone %d relay (pin %d)\n", zoneIdx, relayPins[zoneIdx]);
  relayStates[zoneIdx] = true;
  digitalWrite(relayPins[zoneIdx], HIGH);

  DEBUG_PRINTF("Activating pump relay (pin %d)\n", relayPins[PUMP_IDX]);
  relayStates[PUMP_IDX] = true;
  digitalWrite(relayPins[PUMP_IDX], HIGH);

  currentRunningZone = zoneIdx;
  zoneStartTime = millis();
  zoneDuration = selectedManualDuration * 60000;
  isTimedRun = true;
  currentOperation = OP_MANUAL_ZONE;
  
  selectingDuration = false;

  DEBUG_PRINTF("Zone %d and pump are now ACTIVE\n", zoneIdx);
  DEBUG_PRINTF("Free heap: %d bytes\n", ESP.getFreeHeap());

  navigateTo(STATE_RUNNING_ZONE);
}

void drawRunningZoneMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);

  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 10);
  canvas.println("Zone Running");

  drawDateTimeComponent(canvas, LEFT_PADDING, 10, currentDateTime, getCurrentDayOfWeek());

  unsigned long elapsed = millis() - zoneStartTime;
  unsigned long elapsedSeconds = elapsed / 1000;
  unsigned long elapsedMinutes = elapsedSeconds / 60;
  unsigned long remainingSeconds = elapsedSeconds % 60;

  canvas.setTextSize(2);
  if (currentRunningZone > 0) {
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.setCursor(LEFT_PADDING, 80);
    canvas.printf("Active: %s", systemConfig.zoneNames[currentRunningZone-1]);
    
    canvas.setCursor(LEFT_PADDING, 110);
    canvas.setTextColor(COLOR_ACCENT_PRIMARY);
    canvas.printf("Running: %02lu:%02lu", elapsedMinutes, remainingSeconds);
    
    canvas.setCursor(LEFT_PADDING, 140);
    canvas.setTextColor(relayStates[PUMP_IDX] ? COLOR_SUCCESS : COLOR_ERROR);
    canvas.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    canvas.setTextSize(1);
    canvas.setCursor(LEFT_PADDING, 170);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    if (isTimedRun && zoneDuration > 0) {
      unsigned long totalMinutes = zoneDuration / 60000;
      unsigned long remainingTime = (zoneDuration - elapsed) / 1000;
      unsigned long remMinutes = remainingTime / 60;
      unsigned long remSeconds = remainingTime % 60;
      canvas.printf("Timed run: %lu min total", totalMinutes);
      canvas.setCursor(LEFT_PADDING, 185);
      canvas.setTextColor(COLOR_ACCENT_SECONDARY);
      canvas.printf("Time left: %02lu:%02lu", remMinutes, remSeconds);
    } else {
      canvas.println("Manual run (indefinite)");
    }
  } else {
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(LEFT_PADDING, 80);
    canvas.println("No Zone Active");
  }

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  canvas.setCursor(LEFT_PADDING, 210);
  canvas.println("All Zones:");

  for (int i = 1; i < NUM_RELAYS; i++) {
    int yPos = 225 + (i-1) * 10;
    canvas.setCursor(LEFT_PADDING, yPos);
    
    if (relayStates[i]) {
      canvas.setTextColor(COLOR_SUCCESS);
    } else {
      canvas.setTextColor(COLOR_TEXT_SECONDARY);
    }
    
    canvas.printf("%s: %s", systemConfig.zoneNames[i-1], relayStates[i] ? "ON" : "OFF");
  }

  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 300);
  canvas.println("Press button to stop zone");
}

void stopAllActivity() {
  DEBUG_PRINTLN("=== STOPPING ALL ACTIVITY ===");
  
  for (int i = 1; i < NUM_RELAYS; i++) {
    if (relayStates[i]) {
      DEBUG_PRINTF("Deactivating zone %d (%s) on pin %d\n", i, systemConfig.zoneNames[i-1], relayPins[i]);
    }
    relayStates[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
  
  if (relayStates[PUMP_IDX]) {
    DEBUG_PRINTF("Deactivating pump on pin %d\n", relayPins[PUMP_IDX]);
  }
  relayStates[PUMP_IDX] = false;
  digitalWrite(relayPins[PUMP_IDX], LOW);
  
  currentRunningZone = -1;
  zoneStartTime = 0;
  zoneDuration = 0;
  isTimedRun = false;

  currentRunningCycle = -1;
  currentCycleZoneIndex = -1;
  cycleZoneStartTime = 0;
  cycleInterZoneDelayStartTime = 0;
  inInterZoneDelay = false;
  
  currentOperation = OP_NONE;
  
  DEBUG_PRINTLN("All zones and pump are now OFF. All activity stopped.");
}

// -----------------------------------------------------------------------------
//                       SET SYSTEM TIME (FULLY IMPLEMENTED)
// -----------------------------------------------------------------------------
char setTimeDisplayStrings[7][32];
const char* setTimeDisplayPointers[7];

void drawSetSystemTimeMenu() {
  sprintf(setTimeDisplayStrings[0], "Year  : %d", currentDateTime.year);
  sprintf(setTimeDisplayStrings[1], "Month : %d", currentDateTime.month);
  sprintf(setTimeDisplayStrings[2], "Day   : %d", currentDateTime.day);
  sprintf(setTimeDisplayStrings[3], "Hour  : %d", currentDateTime.hour);
  sprintf(setTimeDisplayStrings[4], "Minute: %d", currentDateTime.minute);
  sprintf(setTimeDisplayStrings[5], "Second: %d", currentDateTime.second);
  sprintf(setTimeDisplayStrings[6], "Back to Settings");

  for (int i = 0; i < 7; i++) {
    setTimeDisplayPointers[i] = setTimeDisplayStrings[i];
  }
  setTimeScrollList.items = setTimeDisplayPointers;

  drawScrollableList(canvas, setTimeScrollList, true);
}

void handleSetSystemTimeEncoder(long diff) {
  if (editingTimeField) {
    switch(timeEditFieldIndex) {
      case 0:
        currentDateTime.year += diff;
        if (currentDateTime.year < MIN_YEAR) currentDateTime.year = MIN_YEAR;
        if (currentDateTime.year > MAX_YEAR) currentDateTime.year = MAX_YEAR;
        break;
      case 1:
        currentDateTime.month += diff;
        if (currentDateTime.month < 1) currentDateTime.month = 12;
        if (currentDateTime.month > 12) currentDateTime.month = 1;
        break;
      case 2:
        currentDateTime.day += diff;
        if (currentDateTime.day < 1) currentDateTime.day = 31;
        if (currentDateTime.day > 31) currentDateTime.day = 1;
        break;
      case 3:
        currentDateTime.hour += diff;
        if (currentDateTime.hour < 0) currentDateTime.hour = 23;
        if (currentDateTime.hour > 23) currentDateTime.hour = 0;
        break;
      case 4:
        currentDateTime.minute += diff;
        if (currentDateTime.minute < 0) currentDateTime.minute = 59;
        if (currentDateTime.minute > 59) currentDateTime.minute = 0;
        break;
      case 5:
        currentDateTime.second += diff;
        if (currentDateTime.second < 0) currentDateTime.second = 59;
        if (currentDateTime.second > 59) currentDateTime.second = 0;
        break;
    }
  } else {
    handleScrollableListInput(setTimeScrollList, diff);
  }
  
  uiDirty = true;
}

void handleSetSystemTimeButton() {
  if (timeEditFieldIndex == setTimeScrollList.num_items) {
    goBack();
    return;
  }

  editingTimeField = !editingTimeField;

  if (!editingTimeField) {
    timeEditFieldIndex++;
    if (timeEditFieldIndex >= setTimeScrollList.num_items) {
        timeEditFieldIndex = 0; 
    }
  }
  
  uiDirty = true;
}

// -----------------------------------------------------------------------------
//          CYCLE A / B / C CONFIG EDIT (ENHANCED)
// -----------------------------------------------------------------------------
void drawCycleConfigMenu(const char* label, CycleConfig& cfg) {
  canvas.fillScreen(COLOR_BACKGROUND);

  // Update the display strings for the zone list before drawing
  for (int i = 0; i < ZONE_COUNT; i++) {
    sprintf(cycleZoneDisplayStrings[i], "%s: %d min", systemConfig.zoneNames[i], cfg.zoneDurations[i]);
  }

  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 10);
  canvas.print(label);
  canvas.println(" Configuration");

  canvas.setTextSize(2);
  uint16_t color;

  color = (cycleEditFieldIndex == 0) ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY;
  canvas.setTextColor(color);
  canvas.setCursor(LEFT_PADDING, 40);
  canvas.printf("Enabled: %s", cfg.enabled ? "YES" : "NO");

  color = (cycleEditFieldIndex >= 1 && cycleEditFieldIndex <= 2) ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY;
  canvas.setTextColor(color);
  canvas.setCursor(LEFT_PADDING, 65);
  canvas.printf("Start Time: %02d:%02d", cfg.startTime.hour, cfg.startTime.minute);

  color = (cycleEditFieldIndex == 3) ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY;
  canvas.setTextColor(color);
  canvas.setCursor(LEFT_PADDING, 90);
  canvas.printf("Inter-Zone Delay: %d min", cfg.interZoneDelay);

  canvas.setTextSize(1);
  canvas.setCursor(LEFT_PADDING, 115);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.println("Days Active:");
  const char* dayLabels[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (int i = 0; i < 7; i++) {
    int xPos = LEFT_PADDING + i * 45;
    color = (cycleEditFieldIndex == (4 + i)) ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY;
    canvas.setTextColor(color);
    canvas.setCursor(xPos, 130);
    canvas.printf("%s %c", dayLabels[i], (cfg.daysActive & (1 << i)) ? '*' : ' ');
  }

  bool is_zone_list_active = (cycleEditFieldIndex >= 11);
  drawScrollableList(canvas, cycleZonesScrollList, is_zone_list_active);

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  canvas.setCursor(LEFT_PADDING, 290);
  canvas.println("Rotate to change, Press to select next.");
  canvas.println("Long press to exit.");
}

void handleCycleEditEncoder(long diff, CycleConfig &cfg, const char* progLabel) {
  if (cycleEditFieldIndex == 11 && editingCycleZone) {
    int selected_zone_index = *cycleZonesScrollList.selected_index_ptr;
    if (selected_zone_index < cycleZonesScrollList.num_items) {
      int newDur = cfg.zoneDurations[selected_zone_index] + diff;
      if (newDur < 0)   newDur = 120;
      if (newDur > 120) newDur = 0;
      cfg.zoneDurations[selected_zone_index] = newDur;
    }
  } else if (cycleEditFieldIndex == 11) {
    handleScrollableListInput(cycleZonesScrollList, diff);
  }
  else {
    switch (cycleEditFieldIndex) {
      case 0:
        if (diff != 0) cfg.enabled = !cfg.enabled;
        break;
      case 1:
        cfg.startTime.hour = (cfg.startTime.hour + diff + 24) % 24;
        break;
      case 2:
        cfg.startTime.minute = (cfg.startTime.minute + diff + 60) % 60;
        break;
      case 3:
        cfg.interZoneDelay += diff;
        if (cfg.interZoneDelay < 0)  cfg.interZoneDelay = 30;
        if (cfg.interZoneDelay > 30) cfg.interZoneDelay = 0;
        break;
      case 4: case 5: case 6: case 7: case 8: case 9: case 10:
        {
          uint8_t dayBit = (1 << (cycleEditFieldIndex - 4));
          if (diff != 0) {
            cfg.daysActive ^= dayBit;
          }
        }
        break;
    }
  }
  
  uiDirty = true;
}

void handleCycleEditButton(CycleConfig &cfg, UIState thisState, const char* progLabel) {
  if (cycleEditFieldIndex == 11) {
    int selected_zone_index = *cycleZonesScrollList.selected_index_ptr;

    if (selected_zone_index == cycleZonesScrollList.num_items) {
      cycleEditFieldIndex = 0;
      editingCycleZone = false;
      saveConfig(); // Save changes when exiting
      goBack();
      return;
    }

    editingCycleZone = !editingCycleZone;

  } else {
    cycleEditFieldIndex++;
  }
  
  if (cycleEditFieldIndex > 10) {
    cycleEditFieldIndex = 11;
  }
  
  uiDirty = true;
}

void startCycleRun(int cycleIndex, ActiveOperationType type) {
  DEBUG_PRINTF("=== STARTING CYCLE %d (%s) ===\n", cycleIndex, cycles[cycleIndex]->name);
  stopAllActivity();

  currentRunningCycle = cycleIndex;
  currentCycleZoneIndex = 0;
  cycleZoneStartTime = millis();
  inInterZoneDelay = false;
  currentOperation = type;

  DEBUG_PRINTF("Cycle %s started. Current operation: %d\n", cycles[cycleIndex]->name, currentOperation);
  navigateTo(STATE_CYCLE_RUNNING);
}

void updateCycleRun() {
  if (currentRunningCycle == -1 || currentOperation == OP_NONE) return;

  CycleConfig* cfg = cycles[currentRunningCycle];
  unsigned long currentTime = millis();

  if (inInterZoneDelay) {
    unsigned long elapsedDelay = currentTime - cycleInterZoneDelayStartTime;
    if (elapsedDelay >= (unsigned long)cfg->interZoneDelay * 60000) {
      inInterZoneDelay = false;
      currentCycleZoneIndex++;
      cycleZoneStartTime = currentTime;
      DEBUG_PRINTF("Inter-zone delay finished. Moving to zone %d\n", currentCycleZoneIndex + 1);
    }
  }

  if (!inInterZoneDelay) {
    if (currentCycleZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentCycleZoneIndex + 1;
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentCycleZoneIndex] * 60000;

      if (!relayStates[zoneToRun] || !relayStates[PUMP_IDX]) {

        DEBUG_PRINTF("Activating cycle zone %d (%s) and pump\n", zoneToRun, systemConfig.zoneNames[zoneToRun-1]);
        relayStates[zoneToRun] = true;
        digitalWrite(relayPins[zoneToRun], HIGH);
        relayStates[PUMP_IDX] = true;
        digitalWrite(relayPins[PUMP_IDX], HIGH);
        cycleZoneStartTime = currentTime;
      }

      unsigned long elapsedZoneTime = currentTime - cycleZoneStartTime;
      if (elapsedZoneTime >= zoneRunDuration) {
        DEBUG_PRINTF("Cycle %s, Zone %d finished. Starting inter-zone delay.\n", cfg->name, zoneToRun);
        relayStates[zoneToRun] = false;
        digitalWrite(relayPins[zoneToRun], LOW);
        
        inInterZoneDelay = true;
        cycleInterZoneDelayStartTime = currentTime;
      }
    } else {
      DEBUG_PRINTF("Cycle %s completed.\n", cfg->name);
      stopAllActivity();
      navigateTo(STATE_MAIN_MENU);
    }
  }
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    uiDirty = true;
  }
}



void drawCycleRunningMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);

  drawDateTimeComponent(canvas, LEFT_PADDING, 10, currentDateTime, getCurrentDayOfWeek());
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);


  if (currentRunningCycle != -1) {
    CycleConfig* cfg = cycles[currentRunningCycle];
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.setRelativeCursor(LEFT_PADDING, TOP_PADDING);
    canvas.printf("Running: %s", cfg->name);

    if (inInterZoneDelay) {
      canvas.setTextColor(COLOR_ACCENT_PRIMARY);
      canvas.setCursor(LEFT_PADDING, 110);
      unsigned long elapsedDelay = (millis() - cycleInterZoneDelayStartTime) / 1000;
      unsigned long remainingDelay = (unsigned long)cfg->interZoneDelay * 60 - elapsedDelay;
      canvas.printf("Delay: %02lu:%02lu", remainingDelay / 60, remainingDelay % 60);
      canvas.setCursor(LEFT_PADDING, 140);
      canvas.setTextColor(COLOR_TEXT_PRIMARY);
      canvas.println("Waiting for next zone...");
    } else if (currentCycleZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentCycleZoneIndex + 1;
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentCycleZoneIndex] * 60000;
      unsigned long elapsedZoneTime = millis() - cycleZoneStartTime;
      unsigned long remainingZoneTime = (zoneRunDuration - elapsedZoneTime) / 1000;

      canvas.setTextColor(COLOR_ACCENT_PRIMARY);
      canvas.setCursor(LEFT_PADDING, 110);
      canvas.printf("Zone %d: %s", zoneToRun, systemConfig.zoneNames[zoneToRun-1]);
      canvas.setCursor(LEFT_PADDING, 140);
      canvas.printf("Time left: %02lu:%02lu", remainingZoneTime / 60, remainingZoneTime % 60);
    } else {
      canvas.setTextColor(COLOR_SUCCESS);
      canvas.setCursor(LEFT_PADDING, 110);
      canvas.println("Cycle Finishing...");
    }

    canvas.setCursor(LEFT_PADDING, 170);
    canvas.setTextColor(relayStates[PUMP_IDX] ? COLOR_SUCCESS : COLOR_ERROR);
    canvas.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.setCursor(LEFT_PADDING, 200);
  } else {
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(LEFT_PADDING, 80);
    canvas.println("No Cycle Active");
  }

  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 300);
  canvas.println("Press button to stop cycle");
}


// -----------------------------------------------------------------------------
//                           Settings Menu Functions
// -----------------------------------------------------------------------------
void drawSettingsMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  
  canvas.setTextSize(1);
  int yPos = 10;

  canvas.setCursor(LEFT_PADDING, yPos);
  if (wifi_manager_is_connected()) {
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.printf("WiFi: %s", wifi_manager_get_ssid().c_str());
    yPos += 12;
    canvas.setCursor(LEFT_PADDING, yPos);
    canvas.printf("IP: %s", wifi_manager_get_ip().c_str());
  } else {
    canvas.setTextColor(COLOR_ERROR);
    canvas.println("WiFi: Not Connected");
    yPos += 12;
    canvas.setCursor(LEFT_PADDING, yPos);
    canvas.println("IP: ---.---.---.---");
  }
  yPos += 15;

  canvas.setCursor(LEFT_PADDING, yPos);
  if (wifi_manager_is_time_synced()) {
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.println("Time: NTP Synced");
  } else {
    canvas.setTextColor(COLOR_WARNING);
    canvas.println("Time: Manual/Software Clock");
  }
  yPos += 15;
  
  canvas.drawLine(0, yPos, 320, yPos, COLOR_TEXT_SECONDARY);
  
  drawScrollableList(canvas, settingsMenuScrollList, true);
}

void drawWiFiSetupLauncherMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  drawScrollableList(canvas, wifiSetupLauncherScrollList, true);
}

void drawWiFiResetMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 10);
  canvas.println("WiFi Reset");

  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  canvas.setCursor(LEFT_PADDING, 50);
  canvas.println("Clearing saved WiFi");
  canvas.println("credentials and");
  canvas.println("restarting device...");

  st7789_push_canvas(canvas.getBuffer(), 320, 240);
  delay(3000);

  wifi_manager_reset_credentials();
}

void drawSystemInfoMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 10);
  canvas.println("System Info");

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  
  int y = 40;
  canvas.setCursor(LEFT_PADDING, y);
  canvas.println("=== Hardware ===");
  y += 15;
  
  canvas.setCursor(LEFT_PADDING, y);
  canvas.printf("Board: ESP32-C6, Rev: %d", ESP.getChipRevision());
  y += 12;
  
  canvas.setCursor(LEFT_PADDING, y);
  canvas.printf("Free Heap: %d bytes", ESP.getFreeHeap());
  y += 20;

  canvas.setCursor(LEFT_PADDING, y);
  canvas.println("=== Network ===");
  y += 15;
  
  if (wifi_manager_is_connected()) {
    canvas.setCursor(LEFT_PADDING, y);
    canvas.printf("SSID: %s", wifi_manager_get_ssid().c_str());
    y += 12;
    
    canvas.setCursor(LEFT_PADDING, y);
    canvas.printf("IP: %s", wifi_manager_get_ip().c_str());
    y += 12;
    
    canvas.setCursor(LEFT_PADDING, y);
    canvas.printf("Signal: %d dBm", wifi_manager_get_rssi());
    y += 12;
    
    canvas.setCursor(LEFT_PADDING, y);
    canvas.printf("MAC: %s", wifi_manager_get_mac_address().c_str());
    y += 20;
  } else {
    canvas.setCursor(LEFT_PADDING, y);
    canvas.println("WiFi: Not Connected");
    y += 20;
  }

  canvas.setCursor(LEFT_PADDING, y);
  canvas.println("=== Time ===");
  y += 15;
  
  canvas.setCursor(LEFT_PADDING, y);
  if (wifi_manager_is_time_synced()) {
    canvas.println("Source: NTP Server");
    y += 12;
    canvas.setCursor(LEFT_PADDING, y);
    unsigned long lastSyncMillis = wifi_manager_get_last_ntp_sync();
    if (lastSyncMillis > 0) {
        canvas.printf("Last Sync: %lu min ago", (millis() - lastSyncMillis) / 60000);
    } else {
        canvas.print("Last Sync: Never");
    }
  } else {
    canvas.println("Source: Software Clock");
  }

  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 225);
  canvas.println("Press button to return");
}

// -----------------------------------------------------------------------------
//                           TEST MODE FUNCTIONS
// -----------------------------------------------------------------------------
void startTestMode() {
  DEBUG_PRINTLN("=== STARTING TEST MODE ===");
  
  testModeActive = true;
  currentTestRelay = 0;
  testModeStartTime = millis();
  
  stopAllActivity();
  
  DEBUG_PRINTF("Turning on relay %d (%s)\n", currentTestRelay, currentTestRelay == 0 ? "Pump" : systemConfig.zoneNames[currentTestRelay-1]);
  relayStates[currentTestRelay] = true;
  digitalWrite(relayPins[currentTestRelay], HIGH);
  
  uiDirty = true;
  
  DEBUG_PRINTLN("Test mode initialized - pump is now ON");
}

void updateTestMode() {
  if (!testModeActive) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - testModeStartTime;
  
  if (elapsed >= TEST_INTERVAL) {
    if (currentTestRelay < NUM_RELAYS) {
      DEBUG_PRINTF("Turning off relay %d (%s)\n", currentTestRelay, currentTestRelay == 0 ? "Pump" : systemConfig.zoneNames[currentTestRelay-1]);
      relayStates[currentTestRelay] = false;
      digitalWrite(relayPins[currentTestRelay], LOW);
    }
    
    currentTestRelay++;
    
    if (currentTestRelay >= NUM_RELAYS) {
      DEBUG_PRINTLN("Test mode complete - all relays tested");
      stopTestMode();
      navigateTo(STATE_MAIN_MENU);
      return;
    }
    
    DEBUG_PRINTF("Turning on relay %d (%s)\n", currentTestRelay, currentTestRelay == 0 ? "Pump" : systemConfig.zoneNames[currentTestRelay-1]);
    relayStates[currentTestRelay] = true;
    digitalWrite(relayPins[currentTestRelay], HIGH);
    
    testModeStartTime = currentTime;
    
    uiDirty = true;
  }
}

void drawTestModeMenu() {
  canvas.fillScreen(COLOR_BACKGROUND);
  
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 10);
  canvas.println("Test Mode");
  
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  canvas.setCursor(LEFT_PADDING, 50);
  
  if (currentTestRelay < NUM_RELAYS) {
    canvas.printf("Testing: %s", currentTestRelay == 0 ? "Pump" : systemConfig.zoneNames[currentTestRelay-1]);
    
    unsigned long elapsed = millis() - testModeStartTime;
    unsigned long remaining = (TEST_INTERVAL - elapsed) / 1000;
    
    canvas.setCursor(LEFT_PADDING, 80);
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.printf("Time left: %lu sec", remaining);
    
    canvas.setCursor(LEFT_PADDING, 110);
    canvas.setTextColor(COLOR_ACCENT_PRIMARY);
    canvas.printf("Relay %d of %d", currentTestRelay + 1, NUM_RELAYS);
  } else {
    canvas.println("Test Complete!");
  }
  
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT_PRIMARY);
  canvas.setCursor(LEFT_PADDING, 150);
  canvas.println("Relay Status:");
  
  for (int i = 0; i < NUM_RELAYS; i++) {
    int yPos = 170 + i * 12;
    canvas.setCursor(LEFT_PADDING, yPos);
    
    if (i == currentTestRelay && testModeActive) {
      canvas.setTextColor(COLOR_SUCCESS);
    } else {
      canvas.setTextColor(COLOR_TEXT_SECONDARY);
    }
    
    canvas.printf("%s: %s", i == 0 ? "Pump" : systemConfig.zoneNames[i-1], relayStates[i] ? "ON" : "OFF");
  }
  
  canvas.setTextColor(COLOR_ACCENT_SECONDARY);
  canvas.setCursor(LEFT_PADDING, 280);
  canvas.println("Press button to cancel test");
}

void stopTestMode() {
  DEBUG_PRINTLN("=== STOPPING TEST MODE ===");
  
  testModeActive = false;
  
  stopAllActivity();
  
  DEBUG_PRINTLN("Test mode stopped - all relays OFF");
}
