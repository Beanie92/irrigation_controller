#include "web_server.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "battery.h"
#include "current_sensor.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "LittleFS.h"

// Define the web server object
AsyncWebServer server(80);

// All the HTML and JS is now served from LittleFS.
// Create a 'data' folder in the root of your Arduino project,
// and place your 'index.html', 'plot.html', 'plot.js', and any other
// web assets there.
//
// Use the 'ESP32 Sketch Data Upload' tool in the Arduino IDE
// (under Tools) to upload the files to the filesystem.
//
// You will need to modify your index.html to get the favicon from
// a file instead of the base64 string.
// e.g. <link rel="icon" href="/logo.webp" type="image/webp">
// and place logo.webp in the data folder.
//
// Also, the line in your javascript:
// for(let i=0; i<${ZONE_COUNT}; i++) {
// needs to be changed to a hardcoded number or fetched from the API,
// as the C++ preprocessor won't replace it in a separate file.
// You can get the zone count from the '/api/status' endpoint's 'relays' array length.

String dayOfWeekToString(uint8_t days) {
    String daysString = "";
    if (days & 0b00000001) daysString += "Su,";
    if (days & 0b00000010) daysString += "Mo,";
    if (days & 0b00000100) daysString += "Tu,";
    if (days & 0b00001000) daysString += "We,";
    if (days & 0b00010000) daysString += "Th,";
    if (days & 0b00100000) daysString += "Fr,";
    if (days & 0b01000000) daysString += "Sa,";
    if (daysString.length() > 0) {
        daysString.remove(daysString.length() - 1); // Remove last comma
    }
    return daysString;
}

// handleRoot and handlePlot are no longer needed as files are served statically.

void handleNotFound(AsyncWebServerRequest *request) {
    Serial.printf("NOT FOUND: %s\n", request->url().c_str());
    request->send(404, "text/plain", "Not found");
}

void handleGetStatus(AsyncWebServerRequest *request) {
    Serial.println("Handling get status request.");
    StaticJsonDocument<1024> doc;
    doc["firmwareVersion"] = "1.0";

    JsonObject dateTimeObj = doc.createNestedObject("dateTime");
    dateTimeObj["year"] = currentDateTime.year;
    dateTimeObj["month"] = currentDateTime.month;
    dateTimeObj["day"] = currentDateTime.day;
    dateTimeObj["hour"] = currentDateTime.hour;
    dateTimeObj["minute"] = currentDateTime.minute;
    dateTimeObj["second"] = currentDateTime.second;
    
    doc["dayOfWeek"] = dayOfWeekToString(getCurrentDayOfWeek());
    doc["batteryLevel"] = batteryLevel;
    doc["wifiRSSI"] = wifi_manager_get_rssi();

    JsonArray relayStatusArray = doc.createNestedArray("relays");
    for (int i = 0; i < NUM_RELAYS; i++) {
        JsonObject relayObj = relayStatusArray.createNestedObject();
        if (i == 0) {
            relayObj["name"] = "Pump";
        } else {
            relayObj["name"] = systemConfig.zoneNames[i-1];
        }
        relayObj["state"] = relayStates[i];
    }
    doc["currentOperation"] = currentOperation;

    JsonObject runningInfo = doc.createNestedObject("runningInfo");
    String operation_description = "Idle";
    String time_elapsed_str = "";
    String time_remaining_str = "";
    unsigned long elapsed_s = 0;
    unsigned long total_duration_s = 0;

    switch(currentOperation) {
        case OP_MANUAL_ZONE: {
            operation_description = "Manual Zone Running: " + String(systemConfig.zoneNames[currentRunningZone-1]);
            elapsed_s = (millis() - zoneStartTime) / 1000;
            total_duration_s = zoneDuration / 1000;
            unsigned long remaining_s = total_duration_s - elapsed_s;
            time_elapsed_str = String(elapsed_s / 60) + "m " + String(elapsed_s % 60) + "s";
            time_remaining_str = String(remaining_s / 60) + "m " + String(remaining_s % 60) + "s";
            break;
        }
        case OP_MANUAL_CYCLE:
        case OP_SCHEDULED_CYCLE: {
            if (currentRunningCycle != -1) {
                CycleConfig* cfg = cycles[currentRunningCycle];
                operation_description = String(cfg->name) + ": Running";
                if (inInterZoneDelay) {
                    elapsed_s = (millis() - cycleInterZoneDelayStartTime) / 1000;
                    total_duration_s = (unsigned long)cfg->interZoneDelay * 60;
                    unsigned long remaining_s = total_duration_s - elapsed_s;
                    time_elapsed_str = String(elapsed_s / 60) + "m " + String(elapsed_s % 60) + "s";
                    time_remaining_str = String(remaining_s / 60) + "m " + String(remaining_s % 60) + "s";
                    runningInfo["is_delay"] = true; // Indicate it's a delay
                    // Add next zone info to description
                    if (currentCycleZoneIndex + 1 < ZONE_COUNT) { // Check if there's a next zone
                        operation_description = String(cfg->name) + ": Delaying " + String(systemConfig.zoneNames[currentCycleZoneIndex + 1]) ;
                    } else {
                        operation_description = String(cfg->name) + ": Delaying Cycle end";
                    }
                } else if (currentCycleZoneIndex < ZONE_COUNT) {
                    elapsed_s = (millis() - cycleZoneStartTime) / 1000;
                    total_duration_s = (unsigned long)cfg->zoneDurations[currentCycleZoneIndex] * 60;
                    unsigned long remaining_s = total_duration_s - elapsed_s;
                    time_elapsed_str = String(elapsed_s / 60) + "m " + String(elapsed_s % 60) + "s";
                    time_remaining_str = String(remaining_s / 60) + "m " + String(remaining_s % 60) + "s";
                    operation_description = String(cfg->name) + ": Running " + String(systemConfig.zoneNames[currentCycleZoneIndex + 1]);
                    runningInfo["is_delay"] = false; // Indicate it's not a delay

                }
            }
            break;
        }
        default:
            runningInfo["is_delay"] = false; // Default for other operations
            break;
    }

    runningInfo["operation"] = currentOperation == OP_NONE ? "OP_NONE" : "OP_RUNNING";
    runningInfo["description"] = operation_description;
    runningInfo["time_elapsed"] = time_elapsed_str;
    runningInfo["time_remaining"] = time_remaining_str;
    runningInfo["elapsed_s"] = elapsed_s;
    runningInfo["total_duration_s"] = total_duration_s;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleGetCurrent(AsyncWebServerRequest *request) {
    // No need to log this every few seconds
    // Serial.println("Handling get current request.");
    StaticJsonDocument<64> doc;
    doc["current"] = read_wcs1800_current();
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleGetCurrentHistory(AsyncWebServerRequest *request) {
    uint32_t since = 0;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }

    const std::vector<CurrentHistoryEntry>& history = get_current_history();
    
    const size_t capacity = JSON_ARRAY_SIZE(history.size()) + history.size() * JSON_OBJECT_SIZE(2);
    DynamicJsonDocument doc(capacity);

    JsonArray historyArray = doc.to<JsonArray>();
    for (const auto& entry : history) {
        uint32_t unix_time = get_unix_time_from_millis(entry.timestamp);
        if (unix_time > since) {
            JsonObject obj = historyArray.createNestedObject();
            obj["timestamp"] = unix_time;
            obj["current"] = entry.current;
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleGetCycles(AsyncWebServerRequest *request) {
    Serial.println("Handling get cycles request.");
    StaticJsonDocument<1024> doc;
    JsonArray cyclesArray = doc.createNestedArray("cycles");

    for (int i = 0; i < NUM_CYCLES; i++) {
        JsonObject cycleObj = cyclesArray.createNestedObject();
        cycleObj["name"] = cycles[i]->name;
        cycleObj["enabled"] = cycles[i]->enabled;
        
        JsonObject startTimeObj = cycleObj.createNestedObject("startTime");
        startTimeObj["hour"] = cycles[i]->startTime.hour;
        startTimeObj["minute"] = cycles[i]->startTime.minute;
        
        cycleObj["daysActive"] = cycles[i]->daysActive;
        cycleObj["daysActiveString"] = dayOfWeekToString(cycles[i]->daysActive);
        cycleObj["interZoneDelay"] = cycles[i]->interZoneDelay;
        
        JsonArray durations = cycleObj.createNestedArray("zoneDurations");
        for (int j = 0; j < ZONE_COUNT; j++) {
            durations.add(cycles[i]->zoneDurations[j]);
        }
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleSetCycle(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling set cycle request.");
    if (index == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);

        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        int cycleIndex = doc["cycleIndex"];
        if (cycleIndex < 0 || cycleIndex >= NUM_CYCLES) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
            return;
        }

        CycleConfig* cfg = cycles[cycleIndex];
        cfg->enabled = doc["enabled"].as<bool>();
        
        JsonObjectConst startTime = doc["startTime"];
        if (startTime) {
            cfg->startTime.hour = startTime["hour"].as<uint8_t>();
            cfg->startTime.minute = startTime["minute"].as<uint8_t>();
        }
        
        cfg->daysActive = doc["daysActive"].as<uint8_t>();
        cfg->interZoneDelay = doc["interZoneDelay"].as<uint8_t>();

        JsonArrayConst zoneDurations = doc["zoneDurations"];
        if (zoneDurations) {
            for (int i = 0; i < ZONE_COUNT && i < zoneDurations.size(); i++) {
                cfg->zoneDurations[i] = zoneDurations[i].as<uint16_t>();
            }
        }
        
        if (saveConfig()) {
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Cycle updated\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false, \"message\":\"Failed to save config\"}");
        }
    }
}

void handleManualControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling manual control request.");
    if (index == 0) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        const char* action = doc["action"];
        if (strcmp(action, "start_zone") == 0) {
            int zone = doc["zone"];
            int duration = doc["duration"];
            if (zone >= 1 && zone <= ZONE_COUNT && duration > 0 && duration <= 120) {
                selectedManualDuration = duration;
                startManualZone(zone);
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Manual zone start requested\"}");
            } else {
                request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid zone or duration\"}");
            }
        } else if (strcmp(action, "start_cycle") == 0) {
            int cycleIdx = doc["cycle"];
            if (cycleIdx >= 0 && cycleIdx < NUM_CYCLES) {
                startCycleRun(cycleIdx, OP_MANUAL_CYCLE);
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Cycle start requested\"}");
            } else {
                request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
            }
        } else if (strcmp(action, "stop_all") == 0) {
            stopAllActivity();
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Stop all requested\"}");
        } else {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Unknown action\"}");
        }
    }
}


void handleGetZoneNames(AsyncWebServerRequest *request) {
    Serial.println("Handling get zone names request.");
    StaticJsonDocument<512> doc;
    JsonArray zoneNamesArray = doc.createNestedArray("zoneNames");
    for (int i = 0; i < ZONE_COUNT; i++) {
        zoneNamesArray.add(systemConfig.zoneNames[i]);
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}


void handleSetZoneNames(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling set zone names request.");
    if (index == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        JsonArrayConst newNames = doc["zoneNames"];
        if (newNames && newNames.size() == ZONE_COUNT) {
            for (int i = 0; i < ZONE_COUNT; i++) {
                strlcpy(systemConfig.zoneNames[i], newNames[i].as<const char*>(), sizeof(systemConfig.zoneNames[i]));
            }
            if (saveConfig()) {
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Zone names updated\"}");
            } else {
                request->send(500, "application/json", "{\"success\":false, \"message\":\"Failed to save config\"}");
            }
        } else {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid data\"}");
        }
    }
}

void handleReset(AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"success\":true, \"message\":\"Restarting...\"}");
    delay(100); // Give the response time to send
    ESP.restart();
}

void initWebServer() {
    Serial.println("Initializing web server...");

    // Initialize LittleFS
    if(!LittleFS.begin(true)){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    Serial.println("Listing files on LittleFS:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
        Serial.print("  FILE: ");
        Serial.print(file.name());
        Serial.print("\tSIZE: ");
        Serial.println(file.size());
        file = root.openNextFile();
    }
    Serial.println("Finished listing files.");

    // Explicitly handle root and plot pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/plot.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/plot.html", "text/html");
    });

    // API Handlers
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/reset", HTTP_POST, handleReset);
    server.on("/api/cycles", HTTP_GET, handleGetCycles);
    server.on("/api/current", HTTP_GET, handleGetCurrent);
    server.on("/api/current_history", HTTP_GET, handleGetCurrentHistory);
    server.on("/api/zonenames", HTTP_GET, handleGetZoneNames);
    server.on("/api/manual", HTTP_POST, [](AsyncWebServerRequest * request){}, NULL, handleManualControl);
    server.on("/api/cycles", HTTP_POST, [](AsyncWebServerRequest * request){}, NULL, handleSetCycle);
    server.on("/api/zonenames", HTTP_POST, [](AsyncWebServerRequest * request){}, NULL, handleSetZoneNames);

    // Serve static files from LittleFS. This should be LAST before notFound
    server.serveStatic("/", LittleFS, "/").setCacheControl("max-age=600");

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started. Static files are served from LittleFS.");
}
