#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include "styling.h"
#include "ui_components.h" // For SystemDateTime

// Initialization and main loop handler
void wifi_manager_init();
void wifi_manager_handle();
void wifi_manager_check_connection();

// Status getters for UI and other modules
bool wifi_manager_is_connected();
bool wifi_manager_is_connecting();
String wifi_manager_get_ssid();
String wifi_manager_get_ip();
String wifi_manager_get_mac_address();
int8_t wifi_manager_get_rssi();
String wifi_manager_get_portal_ssid();
bool wifi_manager_is_time_synced();
unsigned long wifi_manager_get_last_ntp_sync();


// Actions
void wifi_manager_start_portal();
void wifi_manager_reset_credentials();
void wifi_manager_cancel_connection();

// Time synchronization
void wifi_manager_update_system_time(SystemDateTime& dateTime);
uint64_t get_unix_time_ms_from_millis(uint32_t millis_val);

#endif // WIFI_MANAGER_H
