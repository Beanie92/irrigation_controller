
#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include <vector>
#include <cstdint>

struct CurrentHistoryEntry {
  uint64_t timestamp; // Using uint64_t to store milliseconds for long-running applications
  float current;
};

void setup_current_sensor();
float read_wcs1800_current();
void update_current_history();
const std::vector<CurrentHistoryEntry>& get_current_history();

#endif // CURRENT_SENSOR_H
