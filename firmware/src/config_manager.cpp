#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Global instance of the system configuration
SystemConfig systemConfig;

// Path to the configuration file
const char* configFile = "/config.json";

void initializeDefaultConfig() {
    // Default zone names
    for (int i = 0; i < ZONE_COUNT; i++) {
        sprintf(systemConfig.zoneNames[i], "Zone %d", i + 1);
    }

    // Default Cycle A
    systemConfig.cycles[0] = {
        .enabled = true,
        .startTime = {6, 0},
        .daysActive = (DayOfWeek)(MONDAY | WEDNESDAY | FRIDAY),
        .interZoneDelay = 1,
        .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
        .name = "Cycle A"
    };

    // Default Cycle B
    systemConfig.cycles[1] = {
        .enabled = false,
        .startTime = {6, 0},
        .daysActive = (DayOfWeek)(MONDAY | WEDNESDAY | FRIDAY),
        .interZoneDelay = 1,
        .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
        .name = "Cycle B"
    };

    // Default Cycle C
    systemConfig.cycles[2] = {
        .enabled = false,
        .startTime = {6, 0},
        .daysActive = (DayOfWeek)(MONDAY | WEDNESDAY | FRIDAY),
        .interZoneDelay = 1,
        .zoneDurations = {5, 5, 5, 5, 5, 5, 5},
        .name = "Cycle C"
    };
}

bool loadConfig() {
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount file system");
        initializeDefaultConfig();
        return false;
    }

    File file = LittleFS.open(configFile, "r");
    if (!file) {
        Serial.println("Failed to open config file for reading, using defaults.");
        initializeDefaultConfig();
        return false;
    }

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to parse config file, using defaults.");
        initializeDefaultConfig();
        file.close();
        return false;
    }

    // Deserialize zone names
    JsonArrayConst zoneNamesArray = doc["zoneNames"];
    for (int i = 0; i < ZONE_COUNT; i++) {
        if (i < zoneNamesArray.size()) {
            strlcpy(systemConfig.zoneNames[i], zoneNamesArray[i].as<const char*>(), sizeof(systemConfig.zoneNames[i]));
        }
    }

    // Deserialize cycles
    JsonArrayConst cyclesArray = doc["cycles"];
    for (int i = 0; i < 3; i++) {
        if (i < cyclesArray.size()) {
            JsonObjectConst cycleObj = cyclesArray[i];
            systemConfig.cycles[i].enabled = cycleObj["enabled"];
            systemConfig.cycles[i].startTime.hour = cycleObj["startTime"]["hour"];
            systemConfig.cycles[i].startTime.minute = cycleObj["startTime"]["minute"];
            systemConfig.cycles[i].daysActive = cycleObj["daysActive"];
            systemConfig.cycles[i].interZoneDelay = cycleObj["interZoneDelay"];
            strlcpy(systemConfig.cycles[i].name, cycleObj["name"], sizeof(systemConfig.cycles[i].name));

            JsonArrayConst durationsArray = cycleObj["zoneDurations"];
            for (int j = 0; j < ZONE_COUNT; j++) {
                if (j < durationsArray.size()) {
                    systemConfig.cycles[i].zoneDurations[j] = durationsArray[j];
                }
            }
        }
    }

    file.close();
    return true;
}

bool saveConfig() {
    File file = LittleFS.open(configFile, "w");
    if (!file) {
        Serial.println("Failed to open config file for writing");
        return false;
    }

    StaticJsonDocument<2048> doc;

    // Serialize zone names
    JsonArray zoneNamesArray = doc.createNestedArray("zoneNames");
    for (int i = 0; i < ZONE_COUNT; i++) {
        zoneNamesArray.add(systemConfig.zoneNames[i]);
    }

    // Serialize cycles
    JsonArray cyclesArray = doc.createNestedArray("cycles");
    for (int i = 0; i < 3; i++) {
        JsonObject cycleObj = cyclesArray.createNestedObject();
        cycleObj["enabled"] = systemConfig.cycles[i].enabled;
        cycleObj["name"] = systemConfig.cycles[i].name;
        
        JsonObject startTimeObj = cycleObj.createNestedObject("startTime");
        startTimeObj["hour"] = systemConfig.cycles[i].startTime.hour;
        startTimeObj["minute"] = systemConfig.cycles[i].startTime.minute;

        cycleObj["daysActive"] = systemConfig.cycles[i].daysActive;
        cycleObj["interZoneDelay"] = systemConfig.cycles[i].interZoneDelay;

        JsonArray durationsArray = cycleObj.createNestedArray("zoneDurations");
        for (int j = 0; j < ZONE_COUNT; j++) {
            durationsArray.add(systemConfig.cycles[i].zoneDurations[j]);
        }
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to config file");
        file.close();
        return false;
    }

    file.close();
    return true;
}
