
#include <Arduino.h>
#include "DFRobot_GDL.h"

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
static const int relayPins[NUM_RELAYS] = {19, 20, 9, 18, 15, 21, 1, 14}; 
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
  STATE_SET_SYSTEM_TIME,
  STATE_SET_CYCLE_START,
  STATE_PROG_A,
  STATE_PROG_B,
  STATE_PROG_C,
  STATE_RUNNING_ZONE
};

ProgramState currentState = STATE_MAIN_MENU;

// Main Menu Items
static const int MAIN_MENU_ITEMS = 6;
const char* mainMenuLabels[MAIN_MENU_ITEMS] = {
  "Manual Run",
  "Set System Time",
  "Set Cycle Start",
  "Program A",
  "Program B",
  "Program C"
};
int selectedMainMenuIndex = 0; 

// Manual Run zone index: 0..6 => zone = index+1
int selectedManualZoneIndex = 0;

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
struct ProgramConfig {
  // For each of the 7 zones, how long to run (in minutes)
  uint16_t zoneDurations[ZONE_COUNT]; 
  // Delay between zones for that program (in minutes)
  uint8_t interZoneDelay;
};

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
typedef struct {
    bool enabled;           // Whether this program is active
    TimeOfDay startTime;    // When to start the program
    uint8_t daysActive;     // Bitfield using DayOfWeek values
    uint8_t interZoneDelay; // Minutes to wait between zones
    uint16_t zoneRunTimes[ZONE_COUNT]; // Minutes per zone
    char name[16];          // Optional: Program name/description
} IrrigationProgram;

ProgramConfig programA = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneRunTimes = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program A"
};
ProgramConfig programB = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneRunTimes = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program A"
};
ProgramConfig programC = {
    .enabled = true,
    .startTime = {6, 0},  // 6:00 AM
    .daysActive = MONDAY | WEDNESDAY | FRIDAY,
    .interZoneDelay = 1,
    .zoneRunTimes = {5, 5, 5, 5, 5, 5, 5},
    .name = "Program A"
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
//                           Forward Declarations
// -----------------------------------------------------------------------------
void IRAM_ATTR isrPinA();
void handleEncoderMovement();
void handleButtonPress();

void drawMainMenu();
void drawDateTime(int x, int y);
void enterState(ProgramState newState);

void updateSoftwareClock();

// Manual Run
void drawManualRunMenu();
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

// -----------------------------------------------------------------------------
//                                     SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Extended Menu Example - Now fully implemented with sub-menus.");

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

  // Start in main menu
  enterState(STATE_MAIN_MENU);
}

// -----------------------------------------------------------------------------
//                                     LOOP
// -----------------------------------------------------------------------------
void loop() {
  updateSoftwareClock(); 
  handleEncoderMovement();
  handleButtonPress();

  // Additional logic for running states if needed
  if (currentState == STATE_RUNNING_ZONE) {
    // e.g., check time-based zone run or cancellation
  }
}

// -----------------------------------------------------------------------------
//                        INTERRUPT SERVICE ROUTINE
// -----------------------------------------------------------------------------
void IRAM_ATTR isrPinA() {
  bool A = digitalRead(pinA);
  bool B = digitalRead(pinB);
  if (A == B) {
    encoderValue--;
  } else {
    encoderValue++;
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

  switch (currentState) {
    case STATE_MAIN_MENU:
      // Slide through the 6 items
      if (diff > 0) selectedMainMenuIndex++;
      else          selectedMainMenuIndex--;
      if      (selectedMainMenuIndex < 0)               selectedMainMenuIndex = MAIN_MENU_ITEMS - 1;
      else if (selectedMainMenuIndex >= MAIN_MENU_ITEMS) selectedMainMenuIndex = 0;
      drawMainMenu();
      break;

    case STATE_MANUAL_RUN:
      // Slide through zones 1..7
      if (diff > 0) selectedManualZoneIndex++;
      else          selectedManualZoneIndex--;
      if      (selectedManualZoneIndex < 0)           selectedManualZoneIndex = ZONE_COUNT - 1;
      else if (selectedManualZoneIndex >= ZONE_COUNT) selectedManualZoneIndex = 0;
      drawManualRunMenu();
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
      // Usually ignore encoder if a zone is actually running
      break;

    default:
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

      // State-Specific Handling
      switch (currentState) {
        case STATE_MAIN_MENU:
          // User selected an item. Jump to that state:
          switch (selectedMainMenuIndex) {
            case 0: // Manual Run
              enterState(STATE_MANUAL_RUN);
              break;
            case 1: // Set System Time
              enterState(STATE_SET_SYSTEM_TIME);
              break;
            case 2: // Set Cycle Start
              enterState(STATE_SET_CYCLE_START);
              break;
            case 3: // Program A
              enterState(STATE_PROG_A);
              break;
            case 4: // Program B
              enterState(STATE_PROG_B);
              break;
            case 5: // Program C
              enterState(STATE_PROG_C);
              break;
          }
          break;

        case STATE_MANUAL_RUN:
          // Pressing button => Start the selected zone
          startManualZone(selectedManualZoneIndex + 1); // zoneIdx 1..7
          break;

        case STATE_SET_SYSTEM_TIME:
          handleSetSystemTimeButton();
          break;

        case STATE_SET_CYCLE_START:
          handleSetCycleStartButton();
          break;

        case STATE_PROG_A:
          handleProgramEditButton(programA, STATE_PROG_A, "Program A");
          break;

        case STATE_PROG_B:
          handleProgramEditButton(programB, STATE_PROG_B, "Program B");
          break;

        case STATE_PROG_C:
          handleProgramEditButton(programC, STATE_PROG_C, "Program C");
          break;

        case STATE_RUNNING_ZONE:
          // Pressing button => Cancel the running zone
          stopZone();
          enterState(STATE_MAIN_MENU);
          break;

        default:
          break;
      }
    }
  }
  lastButtonState = currentReading;
}

// -----------------------------------------------------------------------------
//                            STATE TRANSITIONS
// -----------------------------------------------------------------------------
void enterState(ProgramState newState) {
  currentState = newState;
  screen.fillScreen(COLOR_RGB565_BLACK);

  switch (currentState) {
    case STATE_MAIN_MENU:
      drawMainMenu();
      break;
    case STATE_MANUAL_RUN:
      // Reset zone selection
      selectedManualZoneIndex = 0;
      drawManualRunMenu();
      break;
    case STATE_SET_SYSTEM_TIME:
      // Start editing from the first field
      timeEditFieldIndex = 0;
      drawSetSystemTimeMenu();
      break;
    case STATE_SET_CYCLE_START:
      // Start editing from the first field
      cycleEditFieldIndex = 0;
      drawSetCycleStartMenu();
      break;
    case STATE_PROG_A:
      programEditZoneIndex = 0;
      drawProgramConfigMenu("Program A", programA);
      break;
    case STATE_PROG_B:
      programEditZoneIndex = 0;
      drawProgramConfigMenu("Program B", programB);
      break;
    case STATE_PROG_C:
      programEditZoneIndex = 0;
      drawProgramConfigMenu("Program C", programC);
      break;
    case STATE_RUNNING_ZONE:
      // Possibly draw a "running zone" screen or countdown
      break;
    default:
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
  Serial.print("Manual Start: Zone ");
  Serial.println(zoneIdx);

  // Turn off any previously running zone
  stopZone();

  // Switch ON the zone
  relayStates[zoneIdx] = true;
  digitalWrite(relayPins[zoneIdx], HIGH);

  // Switch ON the pump
  relayStates[PUMP_IDX] = true;
  digitalWrite(relayPins[PUMP_IDX], HIGH);

  // Move to "running zone" state (indefinite or timed, your choice)
  enterState(STATE_RUNNING_ZONE);
}

void stopZone() {
  // Turn off all zones
  for (int i = 1; i < NUM_RELAYS; i++) {
    relayStates[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
  // Turn off pump
  relayStates[PUMP_IDX] = false;
  digitalWrite(relayPins[PUMP_IDX], LOW);
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
