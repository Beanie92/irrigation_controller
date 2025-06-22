#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "ui_components.h" // For CycleConfig, ZONE_COUNT, NUM_CYCLES

// A structure to hold all persistent configuration
struct SystemConfig {
    char zoneNames[ZONE_COUNT][32];
    CycleConfig cycles[3]; // Using a fixed size based on NUM_CYCLES
};

extern SystemConfig systemConfig;

// Function to initialize the configuration with default values
void initializeDefaultConfig();

// Functions for loading and saving the configuration
bool loadConfig();
bool saveConfig();

#endif // CONFIG_MANAGER_H
