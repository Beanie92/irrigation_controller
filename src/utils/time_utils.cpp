#include "time_utils.h"

TimeKeeper::TimeKeeper() : lastUpdate(0) {
    // Initialize with default time
    currentTime = {2023, 1, 1, 0, 0, 0};
}

void TimeKeeper::update() {
    unsigned long now = millis();
    if ((now - lastUpdate) >= 1000) {
        lastUpdate = now;
        incrementOneSecond();
    }
}

void TimeKeeper::incrementOneSecond() {
    currentTime.second++;
    if (currentTime.second >= 60) {
        currentTime.second = 0;
        currentTime.minute++;
        if (currentTime.minute >= 60) {
            currentTime.minute = 0;
            currentTime.hour++;
            if (currentTime.hour >= 24) {
                currentTime.hour = 0;
                currentTime.day++;
                // Simplified month handling
                if (currentTime.day > 30) {
                    currentTime.day = 1;
                    currentTime.month++;
                    if (currentTime.month > 12) {
                        currentTime.month = 1;
                        currentTime.year++;
                    }
                }
            }
        }
    }
}

SystemDateTime TimeKeeper::getCurrentTime() const {
    return currentTime;
}

void TimeKeeper::setDateTime(const SystemDateTime& dt) {
    currentTime = dt;
}