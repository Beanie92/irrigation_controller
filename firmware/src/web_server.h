#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "ui_components.h" // For SystemDateTime, CycleConfig, DayOfWeek, ZONE_COUNT

extern AsyncWebServer server; // Declare the server object as extern

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
void handleRoot(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);

// API Handlers
void handleGetStatus(AsyncWebServerRequest *request);
void handleGetCycles(AsyncWebServerRequest *request);
void handleSetCycle(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleManualControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// Helper to convert DayOfWeek bitmask to string
String dayOfWeekToString(uint8_t daysActive);
// Helper to parse DayOfWeek string to bitmask
uint8_t stringToDayOfWeek(String daysString);


#endif // WEB_SERVER_H
