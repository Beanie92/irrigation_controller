#include "MenuManager.h"

// -----------------------------------------------------------------------------
//                          STATE TRANSITIONS
// -----------------------------------------------------------------------------
void enterState(ProgramState newState) {
  currentState = newState;
  screen.fillScreen(COLOR_RGB565_BLACK);

  switch (currentState) {
    case STATE_MAIN_MENU:
      drawMainMenu();
      break;
    case STATE_MANUAL_RUN:
      selectedManualZoneIndex = 0;
      drawManualRunMenu();
      break;
    case STATE_SET_SYSTEM_TIME:
      timeEditFieldIndex = 0;
      drawSetSystemTimeMenu();
      break;
    case STATE_SET_CYCLE_START:
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
      // Could draw a "running" screen or countdown
      break;
  }
}

// We need some static indices for certain submenus:
static int timeEditFieldIndex = 0;    // 0=year,1=month,2=day,3=hour,4=minute,5=second
static int cycleEditFieldIndex = 0;   // 0=hour,1=minute
static int programEditZoneIndex = 0;  // 0..7 => (0..6 = zone durations, 7=interZoneDelay)

// -----------------------------------------------------------------------------
//                           MAIN MENU
// -----------------------------------------------------------------------------
void drawMainMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Show date/time at the top
  drawDateTime(10, 10);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 40);
  screen.println("Main Menu");

  // List menu items
  for (int i = 0; i < MAIN_MENU_ITEMS; i++) {
    int yPos = 80 + i * 30;
    uint16_t color = (i == selectedMainMenuIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    screen.println(mainMenuLabels[i]);
  }
}

// A helper to display the current date/time
void drawDateTime(int x, int y) {
  screen.setCursor(x, y);
  screen.setTextColor(COLOR_RGB565_GREEN);
  screen.setTextSize(2);

  char buf[32];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          currentDateTime.year,
          currentDateTime.month,
          currentDateTime.day,
          currentDateTime.hour,
          currentDateTime.minute,
          currentDateTime.second);
  screen.println(buf);
}

// -----------------------------------------------------------------------------
//                           MANUAL RUN
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

  // Relay labels (index 0 is pump, so zone labels start at index 1)
  const char* relayLabels[NUM_RELAYS] = {
    "Pump (auto)",
    "Zone 1",
    "Zone 2",
    "Zone 3",
    "Zone 4",
    "Zone 5",
    "Zone 6",
    "Zone 7"
  };

  // List zones
  for (int i = 0; i < ZONE_COUNT; i++) {
    int yPos = 80 + i * 30;
    uint16_t color = (i == selectedManualZoneIndex) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    screen.print(relayLabels[i + 1]);
    screen.print(": ");
    screen.println(relayStates[i + 1] ? "ON" : "OFF");
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
//                       SET SYSTEM TIME
// -----------------------------------------------------------------------------
void drawSetSystemTimeMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Set System Time");

  auto drawField = [&](int lineIndex, const char* label, int value) {
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

  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 260);
  screen.println("Rotate to change value, Press to next field.");
}

void handleSetSystemTimeEncoder(long diff) {
  switch (timeEditFieldIndex) {
    case 0:  // Year
      currentDateTime.year += diff;
      if (currentDateTime.year < MIN_YEAR) currentDateTime.year = MIN_YEAR;
      if (currentDateTime.year > MAX_YEAR) currentDateTime.year = MAX_YEAR;
      break;
    case 1:  // Month
      currentDateTime.month += diff;
      if (currentDateTime.month < 1) currentDateTime.month = 12;
      if (currentDateTime.month > 12) currentDateTime.month = 1;
      break;
    case 2:  // Day
      currentDateTime.day += diff;
      if (currentDateTime.day < 1) currentDateTime.day = 31;
      if (currentDateTime.day > 31) currentDateTime.day = 1;
      break;
    case 3:  // Hour
      currentDateTime.hour += diff;
      if (currentDateTime.hour < 0) currentDateTime.hour = 23;
      if (currentDateTime.hour > 23) currentDateTime.hour = 0;
      break;
    case 4:  // Minute
      currentDateTime.minute += diff;
      if (currentDateTime.minute < 0) currentDateTime.minute = 59;
      if (currentDateTime.minute > 59) currentDateTime.minute = 0;
      break;
    case 5:  // Second
      currentDateTime.second += diff;
      if (currentDateTime.second < 0) currentDateTime.second = 59;
      if (currentDateTime.second > 59) currentDateTime.second = 0;
      break;
  }
  drawSetSystemTimeMenu();
}

void handleSetSystemTimeButton() {
  timeEditFieldIndex++;
  // If we've edited all 6 fields => done
  if (timeEditFieldIndex > 5) {
    timeEditFieldIndex = 0;
    // Save to NVS
    preferences.begin("myIrrigation", false);
    saveAllToNVS();
    preferences.end();
    // Return to main menu
    enterState(STATE_MAIN_MENU);
  } else {
    drawSetSystemTimeMenu();
  }
}

// -----------------------------------------------------------------------------
//                      SET CYCLE START TIME
// -----------------------------------------------------------------------------
void drawSetCycleStartMenu() {
  screen.fillScreen(COLOR_RGB565_BLACK);

  screen.setTextSize(2);
  screen.setTextColor(COLOR_RGB565_YELLOW);
  screen.setCursor(10, 10);
  screen.println("Set Cycle Start");

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

  screen.setTextSize(1);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setCursor(10, 120);
  screen.println("Rotate to change value, Press to next field.");
}

void handleSetCycleStartEncoder(long diff) {
  switch (cycleEditFieldIndex) {
    case 0:
      cycleStartTime.hour += diff;
      if (cycleStartTime.hour < 0) cycleStartTime.hour = 23;
      if (cycleStartTime.hour > 23) cycleStartTime.hour = 0;
      break;
    case 1:
      cycleStartTime.minute += diff;
      if (cycleStartTime.minute < 0) cycleStartTime.minute = 59;
      if (cycleStartTime.minute > 59) cycleStartTime.minute = 0;
      break;
  }
  drawSetCycleStartMenu();
}

void handleSetCycleStartButton() {
  cycleEditFieldIndex++;
  if (cycleEditFieldIndex > 1) {
    cycleEditFieldIndex = 0;
    // Save to NVS
    preferences.begin("myIrrigation", false);
    saveAllToNVS();
    preferences.end();
    enterState(STATE_MAIN_MENU);
  } else {
    drawSetCycleStartMenu();
  }
}

// -----------------------------------------------------------------------------
//          PROGRAM A / B / C CONFIG EDIT
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
    uint16_t color = (programEditZoneIndex == i) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    char buf[50];
    sprintf(buf, "Zone %d: %d min", i + 1, cfg.zoneDurations[i]);
    screen.println(buf);
  }

  // Draw interZoneDelay
  {
    int i = ZONE_COUNT;  // 7th index for delay
    int yPos = 60 + i * 25;
    uint16_t color = (programEditZoneIndex == i) ? COLOR_RGB565_WHITE : COLOR_RGB565_LGRAY;
    screen.setTextColor(color);
    screen.setCursor(10, yPos);
    char buf[50];
    sprintf(buf, "Delay: %d min", cfg.interZoneDelay);
    screen.println(buf);
  }

  screen.setCursor(10, 250);
  screen.setTextColor(COLOR_RGB565_WHITE);
  screen.setTextSize(1);
  screen.println("Rotate to change value, Press to next field.");
  screen.println("After last field => returns to Main Menu.");
}

void handleProgramEditEncoder(long diff, ProgramConfig& cfg, const char* progLabel) {
  if (programEditZoneIndex < ZONE_COUNT) {
    // Editing zone durations
    int newDur = (int)cfg.zoneDurations[programEditZoneIndex] + diff;
    if (newDur < 0) newDur = 0;
    if (newDur > 120) newDur = 120;
    cfg.zoneDurations[programEditZoneIndex] = newDur;
  } else {
    // editing interZoneDelay
    int newDelay = (int)cfg.interZoneDelay + diff;
    if (newDelay < 0) newDelay = 0;
    if (newDelay > 30) newDelay = 30;
    cfg.interZoneDelay = newDelay;
  }
  drawProgramConfigMenu(progLabel, cfg);
}

void handleProgramEditButton(ProgramConfig& cfg, ProgramState thisState, const char* progLabel) {
  programEditZoneIndex++;
  // We have 7 zones + 1 delay => total 8 fields => indices 0..7
  if (programEditZoneIndex > 7) {
    programEditZoneIndex = 0;
    // Save config
    preferences.begin("myIrrigation", false);
    saveAllToNVS();
    preferences.end();
    // Return to main menu
    enterState(STATE_MAIN_MENU);
  } else {
    drawProgramConfigMenu(progLabel, cfg);
  }
}
