#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <Preferences.h>
#include "web_server.h"
#include "ui_components.h"
#include "styling.h"
#include "st7789_dma_driver.h"
#include "CustomCanvas.h"

#define DEBUG_ENABLED true

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(format, ...)
#endif

extern CustomCanvas canvas;
extern volatile bool encoder_button_pressed;

static WiFiManager wm;
static Preferences preferences;
static bool wifiConnected = false;
static bool timeSync = false;
static unsigned long lastNTPSync = 0;
static const unsigned long NTP_SYNC_INTERVAL = 3600000;
static const char* ntpServer = "pool.ntp.org";
static const long gmtOffset_sec = 7200;
static const int daylightOffset_sec = 0;
static bool isConnecting = false;
static bool portalRunning = false;

static void sync_time_with_ntp();
static void display_wifi_info();
static void display_portal_info();
static void display_connection_success();
static void display_connection_failure(const char* reason);

void wifi_manager_init() {

    DEBUG_PRINTLN("Initializing WiFi Manager in blocking mode...");
    
    isConnecting = true;
    display_wifi_info();

    wm.setDebugOutput(DEBUG_ENABLED);
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(20);
    wm.setCleanConnect(true);
    wm.setConnectRetries(5); // Increase connection retries

    // Custom callback to allow cancellation, though it's less effective in a blocking setup
    wm.setAPCallback([](WiFiManager* myWiFiManager) {
        DEBUG_PRINTLN("Entered config portal mode");
        portalRunning = true;
        display_portal_info();
    });

    // This is a blocking call. It will return when connected or timed out.
    bool res = wm.autoConnect("IrrigationControllerAP", "password123");

    if (res) {
        DEBUG_PRINTLN("WiFi connected successfully via autoConnect!");
        wifiConnected = true;
        display_connection_success();
        sync_time_with_ntp();
        
    } else {
        DEBUG_PRINTLN("Failed to connect and hit timeout.");
        wifiConnected = false;
        display_connection_failure("Auto-connect failed");
    }

    isConnecting = false;
    portalRunning = false;
}

void wifi_manager_handle() {
    // This is now only needed if the portal is running
    if (portalRunning) {
        wm.process();
    }
}

bool wifi_manager_is_connected() {
    return wifiConnected;
}

bool wifi_manager_is_connecting() {
    return isConnecting;
}

String wifi_manager_get_ssid() {
    if (wifiConnected) {
        return WiFi.SSID();
    }
    return wm.getWiFiSSID().length() > 0 ? wm.getWiFiSSID() : "Not Set";
}

String wifi_manager_get_portal_ssid() {
    return "IrrigationControllerAP";
}

String wifi_manager_get_ip() {
    return wifiConnected ? WiFi.localIP().toString() : "---.---.---.---";
}

String wifi_manager_get_mac_address() {
    return WiFi.macAddress();
}

int8_t wifi_manager_get_rssi() {
    return wifiConnected ? WiFi.RSSI() : 0;
}

bool wifi_manager_is_time_synced() {
    return timeSync;
}

unsigned long wifi_manager_get_last_ntp_sync() {
    return lastNTPSync;
}

void wifi_manager_start_portal() {
    DEBUG_PRINTLN("Manual portal start requested.");
    
    // Temporarily disconnect to start the portal
    if(wifiConnected) {
        WiFi.disconnect();
        wifiConnected = false;
    }

    display_portal_info();

    // This is a blocking call
    if (wm.startConfigPortal("IrrigationControllerAP", "password123")) {
        DEBUG_PRINTLN("WiFi config successful via manual portal!");
        wifiConnected = true;
        display_connection_success();
        sync_time_with_ntp();
    } else {
        DEBUG_PRINTLN("WiFi config failed or timed out.");
        display_connection_failure("Portal timed out");
        // Try to reconnect with old credentials if they exist
        wm.autoConnect();
        wifiConnected = WiFi.isConnected();
    }
}

void wifi_manager_reset_credentials() {
    DEBUG_PRINTLN("Clearing WiFi credentials and restarting...");
    wm.resetSettings();
    WiFi.disconnect(true);
    ESP.restart();
}

void wifi_manager_cancel_connection() {
    // This is harder to implement reliably in a fully blocking model from setup()
    // For now, we rely on the portal timeout.
    if (isConnecting && portalRunning) {
        DEBUG_PRINTLN("Connection cancellation requested.");
        wm.stopConfigPortal();
        portalRunning = false;
        isConnecting = false;
        wifiConnected = false;
        display_connection_failure("Cancelled");
        DEBUG_PRINTLN("Portal stopped by user.");
    }
}

void wifi_manager_update_system_time(SystemDateTime& dateTime) {
    if (!timeSync) return;
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    dateTime.year = timeinfo.tm_year + 1900;
    dateTime.month = timeinfo.tm_mon + 1;
    dateTime.day = timeinfo.tm_mday;
    dateTime.hour = timeinfo.tm_hour;
    dateTime.minute = timeinfo.tm_min;
    dateTime.second = timeinfo.tm_sec;
}

static void sync_time_with_ntp() {
    if (!wifiConnected) {
        DEBUG_PRINTLN("Cannot sync time - WiFi not connected.");
        return;
    }
    
    DEBUG_PRINTLN("Initializing NTP time synchronization...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
        timeSync = true;
        lastNTPSync = millis();
        DEBUG_PRINTLN("NTP time synchronization successful!");
    } else {
        timeSync = false;
        DEBUG_PRINTLN("Failed to synchronize with NTP server.");
    }
}

static void display_wifi_info() {
    canvas.fillScreen(COLOR_BACKGROUND);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_WARNING);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Status");
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.setCursor(10, 50);
    canvas.println("Connecting to WiFi...");
    canvas.setCursor(10, 70);
    canvas.printf("SSID: %s", wm.getWiFiSSID().c_str());
    canvas.setCursor(10, 100);
    canvas.setTextColor(COLOR_ACCENT_PRIMARY);
    canvas.println("Press encoder to cancel");
    st7789_push_canvas(canvas.getBuffer(), 320, 240);
}

static void display_portal_info() {
    canvas.fillScreen(COLOR_BACKGROUND);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_WARNING);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Setup Portal");
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.setCursor(10, 50);
    canvas.println("1. Connect to WiFi AP:");
    canvas.setCursor(20, 65);
    canvas.printf("   '%s'", "IrrigationControllerAP");
    canvas.setCursor(10, 85);
    canvas.println("2. Password: 'password123'");
    canvas.setCursor(10, 105);
    canvas.println("3. Open browser to 192.168.4.1");
    canvas.setCursor(10, 140);
    canvas.setTextColor(COLOR_ACCENT_PRIMARY);
    canvas.println("Press encoder to cancel");
    st7789_push_canvas(canvas.getBuffer(), 320, 240);
}

static void display_connection_success() {
    canvas.fillScreen(COLOR_BACKGROUND);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_SUCCESS);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Connected!");
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.setCursor(10, 50);
    canvas.printf("SSID: %s", WiFi.SSID().c_str());
    canvas.setCursor(10, 70);
    canvas.printf("IP: %s", WiFi.localIP().toString().c_str());
    st7789_push_canvas(canvas.getBuffer(), 320, 240);
    delay(2000);
}

static void display_connection_failure(const char* reason) {
    canvas.fillScreen(COLOR_BACKGROUND);
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(10, 10);
    canvas.println("WiFi Failed");
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT_PRIMARY);
    canvas.setCursor(10, 50);
    canvas.printf("Reason: %s", reason);
    st7789_push_canvas(canvas.getBuffer(), 320, 240);
    delay(2000);
}
