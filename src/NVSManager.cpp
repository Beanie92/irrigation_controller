#include "NVSManager.h"

// ----------------------------------------------------------------------------
//   Load configuration from NVS
// ----------------------------------------------------------------------------
void loadAllFromNVS() {
  // System time
  currentDateTime.year   = preferences.getInt("sysYear", currentDateTime.year);
  currentDateTime.month  = preferences.getInt("sysMon",  currentDateTime.month);
  currentDateTime.day    = preferences.getInt("sysDay",  currentDateTime.day);
  currentDateTime.hour   = preferences.getInt("sysHour", currentDateTime.hour);
  currentDateTime.minute = preferences.getInt("sysMin",  currentDateTime.minute);
  currentDateTime.second = preferences.getInt("sysSec",  currentDateTime.second);

  // Cycle start
  cycleStartTime.hour   = preferences.getInt("startHour", cycleStartTime.hour);
  cycleStartTime.minute = preferences.getInt("startMin",  cycleStartTime.minute);

  // Program A
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pA_zone%d", i);
    programA.zoneDurations[i] = preferences.getUInt(key, programA.zoneDurations[i]);
  }
  programA.interZoneDelay = preferences.getUInt("pA_delay", programA.interZoneDelay);

  // Program B
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pB_zone%d", i);
    programB.zoneDurations[i] = preferences.getUInt(key, programB.zoneDurations[i]);
  }
  programB.interZoneDelay = preferences.getUInt("pB_delay", programB.interZoneDelay);

  // Program C
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pC_zone%d", i);
    programC.zoneDurations[i] = preferences.getUInt(key, programC.zoneDurations[i]);
  }
  programC.interZoneDelay = preferences.getUInt("pC_delay", programC.interZoneDelay);
}

// ----------------------------------------------------------------------------
//   Save configuration to NVS
// ----------------------------------------------------------------------------
void saveAllToNVS() {
  // System Time
  preferences.putInt("sysYear", currentDateTime.year);
  preferences.putInt("sysMon",  currentDateTime.month);
  preferences.putInt("sysDay",  currentDateTime.day);
  preferences.putInt("sysHour", currentDateTime.hour);
  preferences.putInt("sysMin",  currentDateTime.minute);
  preferences.putInt("sysSec",  currentDateTime.second);

  // Cycle Start
  preferences.putInt("startHour", cycleStartTime.hour);
  preferences.putInt("startMin",  cycleStartTime.minute);

  // Program A
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pA_zone%d", i);
    preferences.putUInt(key, programA.zoneDurations[i]);
  }
  preferences.putUInt("pA_delay", programA.interZoneDelay);

  // Program B
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pB_zone%d", i);
    preferences.putUInt(key, programB.zoneDurations[i]);
  }
  preferences.putUInt("pB_delay", programB.interZoneDelay);

  // Program C
  for (int i = 0; i < ZONE_COUNT; i++) {
    char key[16];
    sprintf(key, "pC_zone%d", i);
    preferences.putUInt(key, programC.zoneDurations[i]);
  }
  preferences.putUInt("pC_delay", programC.interZoneDelay);

  Serial.println("Configuration saved to NVS.");
}
