# Irrigation Controller Firmware

ESP32-C6 based irrigation controller with WiFi connectivity and NTP time synchronization.

## Features

- **WiFi Connectivity**: Automatic connection to saved WiFi networks
- **NTP Time Synchronization**: Automatic time sync from internet time servers
- **Web Server Interface**: Configure settings, view status, and control zones/cycles via a web browser.
- **Manual Zone Control**: Control individual irrigation zones via rotary encoder interface or web UI.
- **Cycle Scheduling**: Three programmable irrigation schedules (A, B, C) configurable via device UI or web UI.
- **TFT Display**: 240x320 color display with intuitive menu system.
- **Relay Control**: 8-channel relay control (1 pump + 7 zones).
- **Fully Offline Mode**: Operates completely without a WiFi connection, with all features accessible via the device interface.

## Hardware Requirements

- ESP32-C6 development board (DFRobot Beetle ESP32-C6)
- ST7789 240x320 TFT display
- KY-040 rotary encoder with button
- 4 x 2-channel relay module
- Power supply suitable for your relay/pump requirements

## Pin Configuration

### Display (ST7789)
- DC: Pin 2
- CS: Pin 6
- RST: Pin 3

### Rotary Encoder (KY-040)
- CLK: Pin 4
- DT: Pin 7
- SW: Pin 16

### Relays
- Relay 0 (Pump): Pin 19
- Relay 1 (Zone 1): Pin 20
- Relay 2 (Zone 2): Pin 17
- Relay 3 (Zone 3): Pin 18
- Relay 4 (Zone 4): Pin 15
- Relay 5 (Zone 5): Pin 21
- Relay 6 (Zone 6): Pin 8
- Relay 7 (Zone 7): Pin 14

### Current Sensor
-  Anlog Read: Pin 1

## WiFi Setup

The system uses **WiFiManager** with a captive portal for easy WiFi configuration - **WiFi setup is completely optional!**

### Optional WiFi Setup (User-Controlled)

WiFi is now configured through the Settings menu - the system boots directly to the main menu without any WiFi delays.

1. **Navigate to Settings** from the main menu using the rotary encoder
2. **Select "WiFi Setup"** to start the configuration process
3. **The device creates a hotspot**:
   - **Network Name**: `IrrigationController`
   - **Password**: `irrigation123`
4. **Connect your phone/laptop** to the `IrrigationController` network
5. **Open a web browser** - you'll be automatically redirected to the config page
   - If not redirected, go to: `192.168.4.1`
6. **Select your WiFi network** from the list and enter your password
7. **Click Save** - the device will connect to your WiFi and sync time

### Status Display

The device display will show:
- **"WiFi Setup - Connecting..."** when attempting connection
- **"WiFi Setup Required"** with step-by-step instructions when captive portal is active
- **"WiFi Connected!"** with network details when successful
- **"WiFi Setup Timed Out"** if configuration takes too long (3 minutes)

### Reconfiguring WiFi

To change WiFi settings:
1. Use the "WiFi Reset" option in the Settings menu on the device.
2. The captive portal will automatically start again on the next "WiFi Setup" attempt or if the device cannot connect to a previously saved network.

### Troubleshooting WiFi

- **Portal not appearing**: Ensure you're connected to `IrrigationController` network
- **Can't connect to portal**: Try browsing to `192.168.4.1` manually
- **WiFi keeps failing**: Check 2.4GHz network (ESP32 doesn't support 5GHz)
- **Timeout issues**: Portal times out after 3 minutes - restart device to try again

## Time Configuration

The system automatically synchronizes time with NTP servers when WiFi is connected:

- **NTP Server**: pool.ntp.org
- **Timezone**: GMT+2 (Botswana/South Africa)
- **Sync Interval**: Every hour
- **Fallback**: Software clock when WiFi unavailable

To change timezone, modify these values in the code:
```cpp
const long gmtOffset_sec = 7200;     // GMT+2 (adjust as needed)
const int daylightOffset_sec = 0;    // Daylight saving offset
```

## Web Server Interface

Once the ESP32 is connected to your WiFi network, you can access the web interface by navigating to its IP address in a web browser. The IP address is displayed on the device's screen after a successful WiFi connection and also printed to the Serial Monitor.

The web interface allows you to:
- View current system time and relay (zone) statuses.
- Manually set the system date and time.
- Manually start individual zones for a specified duration.
- Stop all currently running irrigation activity.
- View and configure all three irrigation cycles (A, B, C):
    - Enable/disable cycles.
    - Set start time (hour, minute).
    - Set inter-zone delay.
    - Select active days of the week.
    - Configure run durations for each of the 7 zones.
- Manually start a configured cycle.

## Menu System

### Main Menu
- **Manual Run**: Start individual zones manually
- **Settings**: Access system configuration options
- **Set Cycle Start**: Configure cycle start times
- **Cycle A/B/C**: Configure automated irrigation cycles

### Settings Submenu
- **WiFi Setup**: Launch captive portal for WiFi configuration
- **Set Time Manually**: Configure date/time manually (when NTP unavailable)
- **WiFi Reset**: Clear saved WiFi credentials
- **System Info**: View hardware and network status
- **Back to Main Menu**: Return to main menu

### Navigation
- **Rotate Encoder**: Navigate through options
- **Press Button**: Select/confirm option
- **Long Press**: Cancel/return to previous menu

## Programming Irrigation Schedules

Each cycle (A, B, C) can be configured with:
- **Zone Durations**: Individual run times for each zone (0-120 minutes)
- **Inter-zone Delay**: Pause between zones (0-30 minutes)
- **Start Time**: When the cycle should begin
- **Active Days**: Which days of the week to run

## Troubleshooting

### WiFi Connection Issues
1. Check SSID and password are correct
2. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
3. Check signal strength in installation location
4. Monitor serial output for connection status

### Time Sync Issues
1. Verify WiFi connection is working
2. Check internet connectivity
3. Firewall may be blocking NTP (port 123)
4. Try different NTP server if needed

### Display Issues
1. Check all display connections
2. Verify power supply voltage
3. Check SPI pin assignments match your hardware

## Development

### Arduino IDE Setup
1. Install ESP32 board package
2. Select "ESP32C6 Dev Module" as board
3. Install required libraries via the Arduino Library Manager:
   - **Adafruit GFX Library**: A dependency for the display driver.
   - **ArduinoJson**: Used for handling JSON data for configuration.
   - **ESPAsyncWebServer**: Powers the web interface. **Note**: This library seems to work reliably with version 3.7.1.
   - **LittleFS**: For file system storage.
   - **WiFiManager** by tzapu: For easy WiFi configuration.

### Web UI Development

The web interface assets (HTML, JavaScript) are located in the `web-ui` directory. This project uses `webpack` to bundle and optimize the assets.

**Prerequisites:**
- [Node.js and npm](https://nodejs.org/en/download/)

**Workflow:**

1.  **Navigate to the UI directory:**
    ```bash
    cd web-ui
    ```

2.  **Install dependencies** (only needs to be done once):
    ```bash
    npm install
    ```

3.  **Make changes** to the source files located in `web-ui/src` and `web-ui/plot.js`.

4.  **Build the web assets:**
    ```bash
    npm run build
    ```
    This command will:
    - Bundle the JavaScript and HTML files.
    - Copy static assets.
    - Create Gzip-compressed versions of the assets (`.gz`).
    - Place all the final files into the `firmware/data` directory.

5.  **Upload to ESP32:**
    - After building, use the **Tools > ESP32 Sketch Data Upload** option in the Arduino IDE. This will upload the contents of the `data` directory to the device's LittleFS filesystem.

The firmware is configured to automatically serve the gzipped assets, which reduces storage space and improves loading times.

### Serial Debug Output
Enable debug output by setting:
```cpp
#define DEBUG_ENABLED true
```

Connect serial monitor at 115200 baud to see detailed system information.

## Safety Notes

- Always verify relay wiring before connecting pumps/valves
- Use appropriate fuses and circuit protection
- Test manual operation before setting up automated cycles
- Ensure adequate power supply for your specific hardware
- Consider using contactors for high-power pumps

## License

This project is open source. Use and modify as needed for your irrigation requirements.
