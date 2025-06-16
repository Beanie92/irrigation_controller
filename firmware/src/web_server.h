#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h> // Changed from ESPAsyncWebServer
#include <ArduinoJson.h>
#include "ui_components.h" // For SystemDateTime, CycleConfig, DayOfWeek, ZONE_COUNT

extern WebServer server; // Declare the server object as extern

// Forward declarations of functions from src.ino that web server might need to call
extern void startManualZone(int zoneIdx);
extern void startCycleRun(int cycleIndex, ActiveOperationType operationType);
extern void stopAllActivity();
extern DayOfWeek getCurrentDayOfWeek(); // To display current day on web UI

// Extern declarations for global variables from src.ino
extern SystemDateTime currentDateTime;
extern CycleConfig cycleA, cycleB, cycleC;
extern CycleConfig* cycles[]; // Array of pointers to the cycle configs
extern const int NUM_CYCLES;
extern int selectedManualDuration; // For setting duration from web UI before calling startManualZone
extern ActiveOperationType currentOperation; // To show current operation status
extern const char* relayLabels[];
extern const int NUM_RELAYS;
extern bool relayStates[]; // To show current status of relays

// Web server functions
void initWebServer();
void handleRoot(); // Signature changed
void handleNotFound(); // Signature changed

// API Handlers - Signatures will change in .cpp
void handleGetStatus();
void handleGetTime();
void handleSetTime();
void handleGetCycles();
void handleSetCycle(); 
void handleManualControl();

// Helper to convert DayOfWeek bitmask to string
String dayOfWeekToString(uint8_t daysActive);
// Helper to parse DayOfWeek string to bitmask
uint8_t stringToDayOfWeek(String daysString);


#endif // WEB_SERVER_H
