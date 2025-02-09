#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "DFRobot_GDL.h"

// -----------------------------------------------------------------------------
// GLOBAL CONSTANTS / DEFINES
// -----------------------------------------------------------------------------

// Rotary Encoder Inputs
extern const int pinA;     
extern const int pinB;     
extern const int button;   

extern const unsigned long buttonDebounce;

// Relay Pins / Configuration
extern const int NUM_RELAYS;
extern const int relayPins[];
extern bool relayStates[];

extern const int PUMP_IDX;
extern const int ZONE_COUNT;

// Display Pins / Driver
extern const int TFT_DC;
extern const int TFT_CS;
extern const int TFT_RST;
extern DFRobot_ST7789_240x320_HW_SPI screen;

// Menus / Program States
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
extern ProgramState currentState;

// Main Menu Items
extern const int MAIN_MENU_ITEMS;
extern const char* mainMenuLabels[];
extern int selectedMainMenuIndex;

// Manual Run
extern int selectedManualZoneIndex;

// Time-Keeping
struct SystemDateTime {
  int year;    // e.g. 2023
  int month;   // 1..12
  int day;     // 1..31
  int hour;    // 0..23
  int minute;  // 0..59
  int second;  // 0..59
};
extern SystemDateTime currentDateTime;
extern unsigned long lastSecondUpdate;

// Program Config
struct ProgramConfig {
  // For each of the 7 zones, how long to run (in minutes)
  uint16_t zoneDurations[7];
  // Delay between zones (in minutes)
  uint8_t interZoneDelay;
};
extern ProgramConfig programA;
extern ProgramConfig programB;
extern ProgramConfig programC;

// Single "cycleStartTime" for demonstration
extern SystemDateTime cycleStartTime;

// Editor ranges for system time
#define MIN_YEAR 2020
#define MAX_YEAR 2050

// Preferences handle
extern Preferences preferences;

// -----------------------------------------------------------------------------
// GLOBAL FUNCTIONS (prototypes from various .cpps)
// -----------------------------------------------------------------------------

// NVSManager
void loadAllFromNVS();
void saveAllToNVS();

// TimeManager
void incrementOneSecond();
void updateSoftwareClock();

// EncoderManager
void IRAM_ATTR isrPinA();
void handleEncoderMovement();
void handleButtonPress();

// MenuManager
void enterState(ProgramState newState);

void drawMainMenu();
void drawDateTime(int x, int y);

void drawManualRunMenu();
void startManualZone(int zoneIdx);
void stopZone();

void drawSetSystemTimeMenu();
void handleSetSystemTimeEncoder(long diff);
void handleSetSystemTimeButton();

void drawSetCycleStartMenu();
void handleSetCycleStartEncoder(long diff);
void handleSetCycleStartButton();

void drawProgramConfigMenu(const char* label, ProgramConfig& cfg);
void handleProgramEditEncoder(long diff, ProgramConfig& cfg, const char* progLabel);
void handleProgramEditButton(ProgramConfig& cfg, ProgramState thisState, const char* progLabel);

