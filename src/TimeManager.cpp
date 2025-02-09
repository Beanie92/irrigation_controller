#include "TimeManager.h"

// Very naive approach to increment software time once per second
void incrementOneSecond() {
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
        // Simplistic day wrap: always 30 days per month
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

void updateSoftwareClock() {
  unsigned long now = millis();
  if ((now - lastSecondUpdate) >= 1000) {
    lastSecondUpdate = now;
    incrementOneSecond();

    // If in main menu, update displayed time
    if (currentState == STATE_MAIN_MENU) {
      // Overwrite old area
      screen.fillRect(10, 10, 300, 20, COLOR_RGB565_BLACK);
      drawDateTime(10, 10);
    }
  }
}
