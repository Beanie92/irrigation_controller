#include <Arduino.h>
#include "st7789_dma_driver.h" // Replaced DFRobot_GDL.h
#include "logo.h"
#include "ui_components.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <Preferences.h>
#include "web_server.h" // Include the web server header

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

// SPIClass instance for the display. VSPI is commonly used.
// Default VSPI pins for ESP32: SCK=18, MISO=19, MOSI=23, SS=5 (but we use dedicated CS)
// We will pass TFT_CS as the chip select pin to our driver.
// The SPIClass object itself doesn't manage CS, our driver does.
// We will use the default 'SPI' object, which is typically VSPI on ESP32.
// SPIClass spi(VSPI); // This line is removed.

GFXcanvas16 canvas(320, 240);

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

UIState currentState = STATE_MAIN_MENU;
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

// Cycle Config zone selection
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

CycleConfig cycleA = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Cycle A"
};
CycleConfig cycleB = {
    .enabled = false,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Cycle B"
};
CycleConfig cycleC = {
    .enabled = false,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
    .name = "Cycle C"
};

CycleConfig* cycles[] = {&cycleA, &cycleB, &cycleC};
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

// WiFi and NTP functions
void initWiFi();
void connectToWiFi();
void syncTimeWithNTP();
void updateTimeFromNTP();
void updateSystemTimeFromNTP();
void resetWiFiCredentials();
void executeWiFiPortalSetup(); // Renamed from startWiFiSetup

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
  DEBUG_PRINTLN("Initializing display with custom driver...");
  // Initialize SPI for VSPI. For ESP32-C6, check appropriate pins if not default.
  // Assuming default VSPI pins are okay or configured elsewhere if needed.
  // SPI.begin(); // SPIClass begin is called inside st7789_init_display
  // Pass the address of the default SPI object.
  st7789_init_display(TFT_DC, TFT_CS, TFT_RST, TFT_BL, &SPI); 
  canvas.fillScreen(COLOR_RGB565_BLACK); // Clear canvas
  DEBUG_PRINTLN("Display initialized successfully with custom driver");

  // Backlight is handled by st7789_init_display and st7789_set_backlight
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

  // Initialize WiFi and Web Server if enabled/configured
  // For now, we assume WiFiManager handles connection.
  // Web server should start after a successful connection.
  // The initWiFi() function already attempts to connect.
  // We'll call initWebServer() from within connectToWiFi() after success.
  // So, no direct call to initWebServer() here, but ensure connectToWiFi() does it.

  DEBUG_PRINTLN("=== STARTUP COMPLETE ===");
}

// -----------------------------------------------------------------------------
//                                RENDER
// -----------------------------------------------------------------------------
void render() {
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
    case STATE_PROG_A:          drawCycleConfigMenu("Cycle A", cycleA); break;
    case STATE_PROG_B:          drawCycleConfigMenu("Cycle B", cycleB); break;
    case STATE_PROG_C:          drawCycleConfigMenu("Cycle C", cycleC); break;
    case STATE_RUNNING_ZONE:    drawRunningZoneMenu(); break;
    case STATE_CYCLE_RUNNING:   drawCycleRunningMenu(); break;
    case STATE_TEST_MODE:       drawTestModeMenu(); break;
    case STATE_WIFI_SETUP_LAUNCHER: drawWiFiSetupLauncherMenu(); break;
    case STATE_WIFI_RESET:      drawWiFiResetMenu(); break;
    case STATE_SYSTEM_INFO:     drawSystemInfoMenu(); break;
    default:
      // Draw an error screen or something
      canvas.fillScreen(COLOR_RGB565_RED);
      canvas.setCursor(10, 10);
      canvas.setTextSize(2);
      canvas.setTextColor(COLOR_RGB565_WHITE);
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
  // Handle screen dimming
  if (!isScreenDimmed && (millis() - lastActivityTime > inactivityTimeout)) {
    DEBUG_PRINTLN("Screen turning off due to inactivity.");
    st7789_set_backlight(false); // Turn off backlight using driver
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

  // Render the UI if needed
  render();

  // Handle web server client requests
  if (wifiConnected) { // Only handle if WiFi is connected
    server.handleClient();
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
        // Add a small delay to prevent re-triggering in the same second
        delay(1000); 
        break; // Only one cycle can start per second
      }
    }
  }

  // Additional logic for running states if needed
  if (currentState == STATE_RUNNING_ZONE) {
    // Update display every 5 seconds to show current timing
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 5000) {
      lastDisplayUpdate = millis();
      uiDirty = true;
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

  // Restore screen brightness on activity
  if (isScreenDimmed) {
    st7789_set_backlight(true); // Full brightness using driver
    isScreenDimmed = false;
    DEBUG_PRINTLN("Screen brightness restored.");
  }
  lastActivityTime = millis();
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
      handleCycleEditEncoder(diff, cycleA, "Cycle A");
      break;

    case STATE_PROG_B:
      handleCycleEditEncoder(diff, cycleB, "Cycle B");
      break;

    case STATE_PROG_C:
      handleCycleEditEncoder(diff, cycleC, "Cycle C");
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

      // Restore screen brightness on activity
      if (isScreenDimmed) {
        st7789_set_backlight(true); // Full brightness using driver
        isScreenDimmed = false;
        DEBUG_PRINTLN("Screen brightness restored.");
      }
      lastActivityTime = millis();
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
            executeWiFiPortalSetup();
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
          handleCycleEditButton(cycleA, STATE_PROG_A, "Cycle A");
          break;

        case STATE_PROG_B:
          DEBUG_PRINTF("Cycle B button - field %d\n", cycleEditFieldIndex);
          handleCycleEditButton(cycleB, STATE_PROG_B, "Cycle B");
          break;

        case STATE_PROG_C:
          DEBUG_PRINTF("Cycle C button - field %d\n", cycleEditFieldIndex);
          handleCycleEditButton(cycleC, STATE_PROG_C, "Cycle C");
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
  
  // Push the current state to the stack if we are navigating forward
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
      mainMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      mainMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      mainMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      mainMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      mainMenuScrollList.title = "Main Menu";
      mainMenuScrollList.title_text_size = 2;
      mainMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
      mainMenuScrollList.show_back_button = false; // No back button on main menu
      setupScrollableListMetrics(mainMenuScrollList, canvas);
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
      cyclesMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      cyclesMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      cyclesMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      cyclesMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      cyclesMenuScrollList.title = "Cycles";
      cyclesMenuScrollList.title_text_size = 2;
      cyclesMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
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
        cycleSubMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
        cycleSubMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
        cycleSubMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
        cycleSubMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
        cycleSubMenuScrollList.title = progLabel;
        cycleSubMenuScrollList.title_text_size = 2;
        cycleSubMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
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
      // Adjusted Y position based on content in drawSettingsMenu
      // WiFi (2 lines if connected, 2 if not) + Time (1 line) + Spacing + Separator line
      // Approx: 10 (start) + 12 (wifi line1) + 12 (wifi line2/ip) + 15 (spacing) + 12 (time) + 15 (spacing) + 1 (separator) + 5 (padding) = ~72
      // Let's use a calculated yPos from drawSettingsMenu or a fixed offset that accounts for the max height of the top text.
      // For simplicity, using a fixed offset that should be sufficient.
      // Original y was 50. We added IP (1 line) and adjusted spacing.
      // New y should be around 10 (start) + 12 (wifi name) + 12 (ip) + 15 (space) + 12 (time) + 15 (space) + 1 (line) + 5 (padding) = 72
      settingsMenuScrollList.y = 75; // Adjusted to provide enough space for the text above
      settingsMenuScrollList.width = 320;
      settingsMenuScrollList.height = 240 - settingsMenuScrollList.y; // Dynamically adjust height
      settingsMenuScrollList.item_text_size = 2;
      settingsMenuScrollList.item_text_color = COLOR_RGB565_LGRAY;
      settingsMenuScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      settingsMenuScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      settingsMenuScrollList.list_bg_color = COLOR_RGB565_BLACK;
      settingsMenuScrollList.title = "Settings";
      settingsMenuScrollList.title_text_size = 2;
      settingsMenuScrollList.title_text_color = COLOR_RGB565_YELLOW;
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
      setTimeScrollList.item_text_color = COLOR_RGB565_LGRAY;
      setTimeScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      setTimeScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      setTimeScrollList.list_bg_color = COLOR_RGB565_BLACK;
      setTimeScrollList.title = "Set System Time";
      setTimeScrollList.title_text_size = 2;
      setTimeScrollList.title_text_color = COLOR_RGB565_YELLOW;
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
      wifiSetupLauncherScrollList.item_text_color = COLOR_RGB565_LGRAY;
      wifiSetupLauncherScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
      wifiSetupLauncherScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
      wifiSetupLauncherScrollList.list_bg_color = COLOR_RGB565_BLACK;
      wifiSetupLauncherScrollList.title = "WiFi Setup";
      wifiSetupLauncherScrollList.title_text_size = 2;
      wifiSetupLauncherScrollList.title_text_color = COLOR_RGB565_YELLOW;
      wifiSetupLauncherScrollList.show_back_button = false; // "Back" is an explicit item
      setupScrollableListMetrics(wifiSetupLauncherScrollList, canvas);
      DEBUG_PRINTLN("Entering WiFi Setup Launcher");
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
        cycleEditFieldIndex = 0;
        editingCycleZone = false;
        selectedCycleZoneIndex = 0;

        CycleConfig* currentProg;
        const char* progLabel;
        if (newState == STATE_PROG_A) { currentProg = &cycleA; progLabel = "Cycle A"; }
        else if (newState == STATE_PROG_B) { currentProg = &cycleB; progLabel = "Cycle B"; }
        else { currentProg = &cycleC; progLabel = "Cycle C"; }

        cycleZonesScrollList.data_source = currentProg->zoneDurations;
        cycleZonesScrollList.num_items = ZONE_COUNT;
        cycleZonesScrollList.selected_index_ptr = &selectedCycleZoneIndex;
        cycleZonesScrollList.format_string = "Zone %d: %d min";
        cycleZonesScrollList.x = 0;
        cycleZonesScrollList.y = 150;
        cycleZonesScrollList.width = 320;
        cycleZonesScrollList.height = 95;
        cycleZonesScrollList.item_text_size = 2;
        cycleZonesScrollList.item_text_color = COLOR_RGB565_LGRAY;
        cycleZonesScrollList.selected_item_text_color = COLOR_RGB565_WHITE;
        cycleZonesScrollList.selected_item_bg_color = COLOR_RGB565_BLUE;
        cycleZonesScrollList.list_bg_color = COLOR_RGB565_BLACK;
        cycleZonesScrollList.title = "Zone Durations (min)";
        cycleZonesScrollList.title_text_size = 2;
        cycleZonesScrollList.title_text_color = COLOR_RGB565_YELLOW;
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
  canvas.fillScreen(COLOR_RGB565_BLACK);
  // Show date/time at the top (outside the scrollable list component)
  drawDateTimeComponent(canvas, 10, 10, currentDateTime, getCurrentDayOfWeek());
  
  // Draw the scrollable list for the main menu
  drawScrollableList(canvas, mainMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           CYCLES MENU DRAWING
// -----------------------------------------------------------------------------
void drawCyclesMenu() {
  canvas.fillScreen(COLOR_RGB565_BLACK);
  drawScrollableList(canvas, cyclesMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           CYCLE SUB-MENU DRAWING
// -----------------------------------------------------------------------------
void drawCycleSubMenu(const char* label) {
  canvas.fillScreen(COLOR_RGB565_BLACK);
  cycleSubMenuScrollList.title = label;
  drawScrollableList(canvas, cycleSubMenuScrollList, true);
}

// -----------------------------------------------------------------------------
//                           LOGO DISPLAY
// -----------------------------------------------------------------------------
void drawLogo() {
  canvas.fillScreen(COLOR_RGB565_WHITE);
  
  // Calculate position to center the logo
  int x = (320 - LOGO_WIDTH) / 2;
  int y = (240 - LOGO_HEIGHT) / 2;
  
  // Draw the logo image
  for (int row = 0; row < LOGO_HEIGHT; row++) {
    for (int col = 0; col < LOGO_WIDTH; col++) {
      uint16_t pixel = logo_data[row * LOGO_WIDTH + col];
      canvas.drawPixel(x + col, y + row, pixel);
    }
  }
  
  // Add version info below the logo
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_BLACK);
  canvas.setCursor(80, y + LOGO_HEIGHT + 20);
  canvas.println("v1.0 - ESP32-C6");
  
  // Loading indicator
  canvas.setCursor(90, y + LOGO_HEIGHT + 40);
  canvas.println("Loading...");
  updateScreen();
}

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

    // If in a state that shows the clock, mark UI as dirty to redraw
    if (currentState == STATE_MAIN_MENU || currentState == STATE_CYCLE_RUNNING || currentState == STATE_RUNNING_ZONE) {
      uiDirty = true;
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
  canvas.fillScreen(COLOR_RGB565_BLACK);

  if (selectingDuration) {
    // Duration selection mode
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_RGB565_YELLOW);
    canvas.setCursor(10, 10);
    canvas.println("Manual Run");
    
    canvas.setCursor(10, 40);
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.printf("Zone: %s", relayLabels[selectedManualZoneIndex + 1]);
    
    canvas.setCursor(10, 70);
    canvas.setTextColor(COLOR_RGB565_CYAN);
    canvas.println("Select Duration:");
    
    canvas.setCursor(10, 100);
    canvas.setTextSize(3);
    canvas.setTextColor(COLOR_RGB565_WHITE);
    canvas.printf("%d minutes", selectedManualDuration);
    
    // Duration options for reference
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_RGB565_LGRAY);
    canvas.setCursor(10, 140);
    canvas.println("Common durations:");
    canvas.setCursor(10, 155);
    canvas.println("5, 10, 15, 20, 30, 45, 60 min");
    canvas.setCursor(10, 170);
    canvas.println("Range: 1-120 minutes");
    
    // Instructions
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_RGB565_YELLOW);
    canvas.setCursor(10, 200);
    canvas.println("Rotate to adjust duration");
    canvas.setCursor(10, 215);
    canvas.println("Press button to start zone");
    canvas.setCursor(10, 230);
    canvas.println("Long press to go back");
    
  } else {
    // Zone selection mode using scrollable list
    drawScrollableList(canvas, manualRunScrollList, true);
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
  canvas.fillScreen(COLOR_RGB565_BLACK);

  // Title
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("Zone Running");

  // Show current date/time
  drawDateTimeComponent(canvas, 10, 10, currentDateTime, getCurrentDayOfWeek());

  // Calculate elapsed time
  unsigned long elapsed = millis() - zoneStartTime;
  unsigned long elapsedSeconds = elapsed / 1000;
  unsigned long elapsedMinutes = elapsedSeconds / 60;
  unsigned long remainingSeconds = elapsedSeconds % 60;

  // Display running zone information
  canvas.setTextSize(2);
  if (currentRunningZone > 0) {
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.setCursor(10, 80);
    canvas.printf("Active: %s", relayLabels[currentRunningZone]);
    
    // Show elapsed time
    canvas.setCursor(10, 110);
    canvas.setTextColor(COLOR_RGB565_CYAN);
    canvas.printf("Running: %02lu:%02lu", elapsedMinutes, remainingSeconds);
    
    // Show pump status
    canvas.setCursor(10, 140);
    canvas.setTextColor(relayStates[PUMP_IDX] ? COLOR_RGB565_GREEN : COLOR_RGB565_RED);
    canvas.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    // Show run type
    canvas.setTextSize(1);
    canvas.setCursor(10, 170);
    canvas.setTextColor(COLOR_RGB565_WHITE);
    if (isTimedRun && zoneDuration > 0) {
      unsigned long totalMinutes = zoneDuration / 60000;
      unsigned long remainingTime = (zoneDuration - elapsed) / 1000;
      unsigned long remMinutes = remainingTime / 60;
      unsigned long remSeconds = remainingTime % 60;
      canvas.printf("Timed run: %lu min total", totalMinutes);
      canvas.setCursor(10, 185);
      canvas.setTextColor(COLOR_RGB565_YELLOW);
      canvas.printf("Time left: %02lu:%02lu", remMinutes, remSeconds);
    } else {
      canvas.println("Manual run (indefinite)");
    }
  } else {
    canvas.setTextColor(COLOR_RGB565_RED);
    canvas.setCursor(10, 80);
    canvas.println("No Zone Active");
  }

  // Show compact zone status
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 210);
  canvas.println("All Zones:");

  for (int i = 1; i < NUM_RELAYS; i++) {
    int yPos = 225 + (i-1) * 10;
    canvas.setCursor(10, yPos);
    
    // Highlight active zone
    if (relayStates[i]) {
      canvas.setTextColor(COLOR_RGB565_GREEN);
    } else {
      canvas.setTextColor(COLOR_RGB565_LGRAY);
    }
    
    canvas.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
  }

  // Instructions
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 300);
  canvas.println("Press button to stop zone");
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

  // Reset timer variables for cycle run
  currentRunningCycle = -1;
  currentCycleZoneIndex = -1;
  cycleZoneStartTime = 0;
  cycleInterZoneDelayStartTime = 0;
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

  drawScrollableList(canvas, setTimeScrollList, true);
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
  uiDirty = true;
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
  
  uiDirty = true;
}

// -----------------------------------------------------------------------------
//          CYCLE A / B / C CONFIG EDIT (ENHANCED)
// -----------------------------------------------------------------------------
void drawCycleConfigMenu(const char* label, CycleConfig& cfg) {
  canvas.fillScreen(COLOR_RGB565_BLACK);

  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.print(label);
  canvas.println(" Configuration");

  // --- Draw non-scrolling fields ---
  canvas.setTextSize(2);
  uint16_t color;

  // Field 0: Enabled
  color = (cycleEditFieldIndex == 0) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  canvas.setTextColor(color);
  canvas.setCursor(10, 40);
  canvas.printf("Enabled: %s", cfg.enabled ? "YES" : "NO");

  // Field 1 & 2: Start Time
  color = (cycleEditFieldIndex >= 1 && cycleEditFieldIndex <= 2) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  canvas.setTextColor(color);
  canvas.setCursor(10, 65);
  canvas.printf("Start Time: %02d:%02d", cfg.startTime.hour, cfg.startTime.minute);

  // Field 3: Inter-Zone Delay
  color = (cycleEditFieldIndex == 3) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
  canvas.setTextColor(color);
  canvas.setCursor(10, 90);
  canvas.printf("Inter-Zone Delay: %d min", cfg.interZoneDelay);

  // Fields 4-10: Days Active
  canvas.setTextSize(1);
  canvas.setCursor(10, 115);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.println("Days Active:");
  const char* dayLabels[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (int i = 0; i < 7; i++) {
    int xPos = 10 + i * 45;
    color = (cycleEditFieldIndex == (4 + i)) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    canvas.setTextColor(color);
    canvas.setCursor(xPos, 130);
    canvas.printf("%s %c", dayLabels[i], (cfg.daysActive & (1 << i)) ? '*' : ' ');
  }

  // --- Draw scrollable list for Zone Durations ---
  bool is_zone_list_active = (cycleEditFieldIndex >= 11);
  drawScrollableList(canvas, cycleZonesScrollList, is_zone_list_active);


  // --- Instructions ---
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 290);
  canvas.println("Rotate to change, Press to select next.");
  canvas.println("Long press to exit.");
}

void handleCycleEditEncoder(long diff, CycleConfig &cfg, const char* progLabel) {
  // New field indices: 0=enabled, 1=hour, 2=minute, 3=interZoneDelay, 4-10=days, 11=zone list
  if (cycleEditFieldIndex == 11 && editingCycleZone) {
    // We are editing a specific zone's duration
    int selected_zone_index = *cycleZonesScrollList.selected_index_ptr;
    if (selected_zone_index < cycleZonesScrollList.num_items) {
      int newDur = cfg.zoneDurations[selected_zone_index] + diff;
      if (newDur < 0)   newDur = 120;
      if (newDur > 120) newDur = 0;
      cfg.zoneDurations[selected_zone_index] = newDur;
    }
  } else if (cycleEditFieldIndex == 11) {
    // We are navigating the zone list
    handleScrollableListInput(cycleZonesScrollList, diff);
  }
  else {
    // Handle non-list fields
    switch (cycleEditFieldIndex) {
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
          uint8_t dayBit = (1 << (cycleEditFieldIndex - 4));
          if (diff != 0) { // Only toggle on movement
            cfg.daysActive ^= dayBit; // Toggle the bit
          }
        }
        break;
    }
  }
  
  uiDirty = true;
}

void handleCycleEditButton(CycleConfig &cfg, UIState thisState, const char* progLabel) {
  // If we are in the zone list (field index 11)
  if (cycleEditFieldIndex == 11) {
    int selected_zone_index = *cycleZonesScrollList.selected_index_ptr;

    // If the "Back" button is selected, go back.
    if (selected_zone_index == cycleZonesScrollList.num_items) {
      cycleEditFieldIndex = 0; // Reset for next time
      editingCycleZone = false;
      goBack();
      return;
    }

    // Toggle editing mode for the selected zone
    editingCycleZone = !editingCycleZone;

    // If we are NOT editing anymore, it means we've confirmed the value.
    // Let's just stay in the list to allow editing another zone.
    // A long press should be used to exit, which is handled by goBack().
    // Or the user can select the "Back" item.

  } else {
    // Otherwise, advance to the next field
    cycleEditFieldIndex++;
  }
  
  // If we've moved past the simple fields (0-10), we enter the zone list editing mode (11).
  if (cycleEditFieldIndex > 10) {
    cycleEditFieldIndex = 11; // Cap at 11
  }
  
  uiDirty = true;
}

void startCycleRun(int cycleIndex, ActiveOperationType type) {
  DEBUG_PRINTF("=== STARTING CYCLE %d (%s) ===\n", cycleIndex, cycles[cycleIndex]->name);
  stopAllActivity(); // Ensure only one operation runs at a time

  currentRunningCycle = cycleIndex;
  currentCycleZoneIndex = 0; // Start with the first zone
  cycleZoneStartTime = millis();
  inInterZoneDelay = false;
  currentOperation = type; // OP_MANUAL_CYCLE or OP_SCHEDULED_CYCLE

  DEBUG_PRINTF("Cycle %s started. Current operation: %d\n", cycles[cycleIndex]->name, currentOperation);
  navigateTo(STATE_CYCLE_RUNNING);
}

void updateCycleRun() {
  if (currentRunningCycle == -1 || currentOperation == OP_NONE) return;

  CycleConfig* cfg = cycles[currentRunningCycle];
  unsigned long currentTime = millis();

  if (inInterZoneDelay) {
    unsigned long elapsedDelay = currentTime - cycleInterZoneDelayStartTime;
    if (elapsedDelay >= (unsigned long)cfg->interZoneDelay * 60000) { // Delay in minutes
      inInterZoneDelay = false;
      currentCycleZoneIndex++; // Move to next zone
      cycleZoneStartTime = currentTime; // Reset timer for new zone
      DEBUG_PRINTF("Inter-zone delay finished. Moving to zone %d\n", currentCycleZoneIndex + 1);
    }
  }

  // Check if current zone needs to be activated or timed out
  if (!inInterZoneDelay) {
    if (currentCycleZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentCycleZoneIndex + 1; // Zones are 1-indexed
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentCycleZoneIndex] * 60000; // Minutes to milliseconds

      // Activate zone and pump if not already active
      if (!relayStates[zoneToRun] || !relayStates[PUMP_IDX]) {
        stopAllActivity(); // Ensure clean state before activating
        DEBUG_PRINTF("Activating cycle zone %d (%s) and pump\n", zoneToRun, relayLabels[zoneToRun]);
        relayStates[zoneToRun] = true;
        digitalWrite(relayPins[zoneToRun], HIGH);
        relayStates[PUMP_IDX] = true;
        digitalWrite(relayPins[PUMP_IDX], HIGH);
        cycleZoneStartTime = currentTime; // Ensure timer starts when relays are actually on
      }

      unsigned long elapsedZoneTime = currentTime - cycleZoneStartTime;
      if (elapsedZoneTime >= zoneRunDuration) {
        DEBUG_PRINTF("Cycle %s, Zone %d finished. Starting inter-zone delay.\n", cfg->name, zoneToRun);
        // Turn off current zone, but keep pump on for delay if needed
        relayStates[zoneToRun] = false;
        digitalWrite(relayPins[zoneToRun], LOW);
        
        inInterZoneDelay = true;
        cycleInterZoneDelayStartTime = currentTime;
      }
    } else {
      // All zones in the cycle are complete
      DEBUG_PRINTF("Cycle %s completed.\n", cfg->name);
      stopAllActivity();
      navigateTo(STATE_MAIN_MENU);
    }
  }
  // Update display every 5 seconds
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    uiDirty = true;
  }
}

void drawCycleRunningMenu() {
  canvas.fillScreen(COLOR_RGB565_BLACK);

  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("Cycle Running");

  drawDateTimeComponent(canvas, 10, 10, currentDateTime, getCurrentDayOfWeek());

  if (currentRunningCycle != -1) {
    CycleConfig* cfg = cycles[currentRunningCycle];
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.setCursor(10, 80);
    canvas.printf("Cycle: %s", cfg->name);

    if (inInterZoneDelay) {
      canvas.setTextColor(COLOR_RGB565_CYAN);
      canvas.setCursor(10, 110);
      unsigned long elapsedDelay = (millis() - cycleInterZoneDelayStartTime) / 1000;
      unsigned long remainingDelay = (unsigned long)cfg->interZoneDelay * 60 - elapsedDelay;
      canvas.printf("Delay: %02lu:%02lu", remainingDelay / 60, remainingDelay % 60);
      canvas.setCursor(10, 140);
      canvas.setTextColor(COLOR_RGB565_WHITE);
      canvas.println("Waiting for next zone...");
    } else if (currentCycleZoneIndex < ZONE_COUNT) {
      int zoneToRun = currentCycleZoneIndex + 1;
      unsigned long zoneRunDuration = (unsigned long)cfg->zoneDurations[currentCycleZoneIndex] * 60000;
      unsigned long elapsedZoneTime = millis() - cycleZoneStartTime;
      unsigned long remainingZoneTime = (zoneRunDuration - elapsedZoneTime) / 1000;

      canvas.setTextColor(COLOR_RGB565_CYAN);
      canvas.setCursor(10, 110);
      canvas.printf("Zone %d: %s", zoneToRun, relayLabels[zoneToRun]);
      canvas.setCursor(10, 140);
      canvas.printf("Time left: %02lu:%02lu", remainingZoneTime / 60, remainingZoneTime % 60);
    } else {
      canvas.setTextColor(COLOR_RGB565_GREEN);
      canvas.setCursor(10, 110);
      canvas.println("Cycle Finishing...");
    }

    canvas.setCursor(10, 170);
    canvas.setTextColor(relayStates[PUMP_IDX] ? COLOR_RGB565_GREEN : COLOR_RGB565_RED);
    canvas.printf("Pump: %s", relayStates[PUMP_IDX] ? "ON" : "OFF");

    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_RGB565_WHITE);
    canvas.setCursor(10, 200);
    canvas.println("Current Status:");
    for (int i = 1; i < NUM_RELAYS; i++) {
      int yPos = 215 + (i-1) * 10;
      canvas.setCursor(10, yPos);
      if (relayStates[i]) {
        canvas.setTextColor(COLOR_RGB565_GREEN);
      } else {
        canvas.setTextColor(COLOR_RGB565_LGRAY);
      }
      canvas.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
    }
  } else {
    canvas.setTextColor(COLOR_RGB565_RED);
    canvas.setCursor(10, 80);
    canvas.println("No Cycle Active");
  }

  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 300);
  canvas.println("Press button to stop cycle");
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
  canvas.fillScreen(COLOR_RGB565_BLACK);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("WiFi Setup");
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 50);
  canvas.println("Connecting...");
  updateScreen();
  
  DEBUG_PRINTLN("Attempting to connect to saved WiFi...");
  
  // Try to connect with saved credentials first
  if (!wm.autoConnect("IrrigationController", "irrigation123")) {
    DEBUG_PRINTLN("Failed to connect to WiFi");
    
    // Show captive portal instructions on display
    canvas.fillScreen(COLOR_RGB565_BLACK);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_RGB565_RED);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Setup Required");
    
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_RGB565_WHITE);
    canvas.setCursor(10, 50);
    canvas.println("1. Connect to WiFi:");
    canvas.setCursor(10, 70);
    canvas.println("   'IrrigationController'");
    canvas.setCursor(10, 90);
    canvas.println("2. Password: irrigation123");
    canvas.setCursor(10, 110);
    canvas.println("3. Open browser to:");
    canvas.setCursor(10, 130);
    canvas.println("   192.168.4.1");
    canvas.setCursor(10, 150);
    canvas.println("4. Configure your WiFi");
    canvas.setCursor(10, 170);
    canvas.println("5. Device will restart");
    
    canvas.setTextColor(COLOR_RGB565_YELLOW);
    canvas.setCursor(10, 200);
    canvas.println("Waiting for config...");
    updateScreen();
    
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
      canvas.fillScreen(COLOR_RGB565_BLACK);
      canvas.setTextSize(2);
      canvas.setTextColor(COLOR_RGB565_RED);
      canvas.setCursor(10, 10);
      canvas.println("WiFi Setup");
      canvas.println("Timed Out");
      canvas.setTextColor(COLOR_RGB565_WHITE);
      canvas.setCursor(10, 80);
      canvas.println("Continuing without");
      canvas.println("internet time sync");
      updateScreen();
      delay(3000);
    }
  } else {
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
    canvas.fillScreen(COLOR_RGB565_BLACK);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Connected!");
    
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_RGB565_WHITE);
    canvas.setCursor(10, 50);
    canvas.printf("SSID: %s\n", WiFi.SSID().c_str());
    canvas.setCursor(10, 70);
    canvas.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    canvas.setCursor(10, 90);
    canvas.println("Syncing time...");
    
  // Initialize NTP
  syncTimeWithNTP();
  
  // Show final status
  canvas.setCursor(10, 110);
  if (timeSync) {
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.println("Time sync: SUCCESS");
  } else {
    canvas.setTextColor(COLOR_RGB565_YELLOW);
    canvas.println("Time sync: FAILED");
  }

  // Initialize Web Server
  initWebServer(); // Call this after WiFi is confirmed connected.

  st7789_push_canvas(canvas.getBuffer(), 320, 240); // Use new driver
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
  canvas.fillScreen(COLOR_RGB565_BLACK);
  
  // --- Draw the static status information at the top ---
  canvas.setTextSize(1);
  int yPos = 10; // Starting Y position for status text

  // WiFi Status & IP Address
  canvas.setCursor(10, yPos);
  if (wifiConnected) {
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.printf("WiFi: %s", WiFi.SSID().c_str());
    yPos += 12; // Move down for next line
    canvas.setCursor(10, yPos);
    canvas.printf("IP: %s", WiFi.localIP().toString().c_str());
  } else {
    canvas.setTextColor(COLOR_RGB565_RED);
    canvas.println("WiFi: Not Connected");
    yPos += 12; // Move down for next line
    canvas.setCursor(10, yPos);
    canvas.println("IP: ---.---.---.---");
  }
  yPos += 15; // Add a bit more space before Time status

  // Time Sync Status
  canvas.setCursor(10, yPos);
  if (timeSync) {
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.println("Time: NTP Synced");
  } else {
    canvas.setTextColor(COLOR_RGB565_YELLOW);
    canvas.println("Time: Manual/Software Clock");
  }
  yPos += 15; // Space before separator line
  
  // Draw a separator line
  canvas.drawLine(0, yPos, 320, yPos, COLOR_RGB565_LGRAY);
  
  // Adjust the Y position of the scrollable list based on the new yPos
  // This will be done in the navigateTo function where the list is configured.
  // For now, we just ensure the drawing function itself is correct.
  // The actual y position for drawing the list items is handled by drawScrollableList.

  // --- Draw the scrollable list for the settings items ---
  // The y position of the list itself (settingsMenuScrollList.y) will be set in navigateTo
  drawScrollableList(canvas, settingsMenuScrollList, true);
}

void drawWiFiSetupLauncherMenu() { // Renamed from drawWiFiSetupMenu
  canvas.fillScreen(COLOR_RGB565_BLACK);
  // Additional info can be drawn here if needed, above or below the list
  drawScrollableList(canvas, wifiSetupLauncherScrollList, true);
}

void executeWiFiPortalSetup() { // Renamed from startWiFiSetup
  canvas.fillScreen(COLOR_RGB565_BLACK);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("WiFi Setup");

  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 50);
  canvas.println("Starting WiFi");
  canvas.println("configuration...");
  st7789_push_canvas(canvas.getBuffer(), 320, 240); // Use new driver

  DEBUG_PRINTLN("=== Manual WiFi Setup ===");
  
  // Create WiFiManager instance
  WiFiManager wm;
  wm.setDebugOutput(DEBUG_ENABLED);
  wm.setConfigPortalTimeout(180); // 3 minutes
  
  // Show setup instructions
  canvas.fillScreen(COLOR_RGB565_BLACK);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("WiFi Setup Portal");
  
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 50);
  canvas.println("1. Connect to WiFi:");
  canvas.setCursor(10, 70);
  canvas.println("   'IrrigationController'");
  canvas.setCursor(10, 90);
  canvas.println("2. Password: irrigation123");
  canvas.setCursor(10, 110);
  canvas.println("3. Open browser to:");
  canvas.setCursor(10, 130);
  canvas.println("   192.168.4.1");
  canvas.setCursor(10, 150);
  canvas.println("4. Configure your WiFi");
  
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 180);
  canvas.println("Starting portal...");
  st7789_push_canvas(canvas.getBuffer(), 320, 240); // Use new driver
  
  // Start configuration portal
  if (wm.startConfigPortal("IrrigationController", "irrigation123")) {
    DEBUG_PRINTLN("WiFi configuration successful!");
    wifiConnected = true;
    connectToWiFi();
  } else {
    DEBUG_PRINTLN("WiFi configuration failed or timed out");
    canvas.fillScreen(COLOR_RGB565_BLACK);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_RGB565_RED);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Setup");
    canvas.println("Failed");
    canvas.setTextColor(COLOR_RGB565_WHITE);
    canvas.setCursor(10, 80);
    canvas.println("Try again later");
    st7789_push_canvas(canvas.getBuffer(), 320, 240); // Use new driver
    delay(2000);
  }
  
  // Return to settings menu
  navigateTo(STATE_SETTINGS);
}

void drawWiFiResetMenu() {
  canvas.fillScreen(COLOR_RGB565_BLACK);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("WiFi Reset");

  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 50);
  canvas.println("Clearing saved");
  canvas.println("WiFi credentials...");

  DEBUG_PRINTLN("=== WiFi Reset ===");
  resetWiFiCredentials();

  canvas.setTextColor(COLOR_RGB565_GREEN);
  canvas.setCursor(10, 120);
  canvas.println("WiFi credentials");
  canvas.println("cleared!");

  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 180);
  canvas.println("Use 'WiFi Setup' to");
  canvas.println("configure new network");
  st7789_push_canvas(canvas.getBuffer(), 320, 240); // Use new driver
  delay(3000);
  navigateTo(STATE_SETTINGS);
}

void drawSystemInfoMenu() {
  canvas.fillScreen(COLOR_RGB565_BLACK);
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("System Info");

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  
  int y = 40; // Adjusted starting y
  canvas.setCursor(10, y);
  canvas.println("=== Hardware ===");
  y += 15;
  
  canvas.setCursor(10, y);
  canvas.printf("Board: ESP32-C6, Rev: %d", ESP.getChipRevision());
  y += 12;
  
  canvas.setCursor(10, y);
  canvas.printf("Free Heap: %d bytes", ESP.getFreeHeap());
  y += 20;

  canvas.setCursor(10, y);
  canvas.println("=== Network ===");
  y += 15;
  
  if (wifiConnected) {
    canvas.setCursor(10, y);
    canvas.printf("SSID: %s", WiFi.SSID().c_str());
    y += 12;
    
    canvas.setCursor(10, y);
    canvas.printf("IP: %s", WiFi.localIP().toString().c_str());
    y += 12;
    
    canvas.setCursor(10, y);
    canvas.printf("Signal: %d dBm", WiFi.RSSI());
    y += 12;
    
    canvas.setCursor(10, y);
    canvas.printf("MAC: %s", WiFi.macAddress().c_str());
    y += 20;
  } else {
    canvas.setCursor(10, y);
    canvas.println("WiFi: Not Connected");
    y += 20;
  }

  canvas.setCursor(10, y);
  canvas.println("=== Time ===");
  y += 15;
  
  canvas.setCursor(10, y);
  if (timeSync) {
    canvas.println("Source: NTP Server");
    y += 12;
    canvas.setCursor(10, y);
    canvas.printf("Last Sync: %lu min ago", (millis() - lastNTPSync) / 60000);
  } else {
    canvas.println("Source: Software Clock");
  }

  // Instructions
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 225);
  canvas.println("Press button to return");
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
  uiDirty = true;
  
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
    uiDirty = true;
  }
}

void drawTestModeMenu() {
  canvas.fillScreen(COLOR_RGB565_BLACK);
  
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 10);
  canvas.println("Test Mode");
  
  // Show current status
  canvas.setTextSize(2);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 50);
  
  if (currentTestRelay < NUM_RELAYS) {
    canvas.printf("Testing: %s", relayLabels[currentTestRelay]);
    
    // Show countdown
    unsigned long elapsed = millis() - testModeStartTime;
    unsigned long remaining = (TEST_INTERVAL - elapsed) / 1000;
    
    canvas.setCursor(10, 80);
    canvas.setTextColor(COLOR_RGB565_GREEN);
    canvas.printf("Time left: %lu sec", remaining);
    
    // Show progress
    canvas.setCursor(10, 110);
    canvas.setTextColor(COLOR_RGB565_CYAN);
    canvas.printf("Relay %d of %d", currentTestRelay + 1, NUM_RELAYS);
  } else {
    canvas.println("Test Complete!");
  }
  
  // Show all relay states
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_RGB565_WHITE);
  canvas.setCursor(10, 150);
  canvas.println("Relay Status:");
  
  for (int i = 0; i < NUM_RELAYS; i++) {
    int yPos = 170 + i * 12;
    canvas.setCursor(10, yPos);
    
    // Highlight current relay
    if (i == currentTestRelay && testModeActive) {
      canvas.setTextColor(COLOR_RGB565_GREEN);
    } else {
      canvas.setTextColor(COLOR_RGB565_LGRAY);
    }
    
    canvas.printf("%s: %s", relayLabels[i], relayStates[i] ? "ON" : "OFF");
  }
  
  // Instructions
  canvas.setTextColor(COLOR_RGB565_YELLOW);
  canvas.setCursor(10, 280);
  canvas.println("Press button to cancel test");
}

void stopTestMode() {
  DEBUG_PRINTLN("=== STOPPING TEST MODE ===");
  
  testModeActive = false;
  
  stopAllActivity(); // Ensure all relays are off
  
  DEBUG_PRINTLN("Test mode stopped - all relays OFF");
}
