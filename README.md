# Hydr8: Irrigation Controller

![image](https://github.com/user-attachments/assets/09214a03-ba51-4387-acdd-ce2d5f2400e1)

An ESP32-C6 based irrigation controller with a web interface, manual controls, and flexible scheduling.

## Features

- **WiFi & Web UI**: Configure and control the system from any web browser.
- **Manual Control**: Use the physical rotary encoder for on-the-spot adjustments.
- **Flexible Scheduling**: Set up to three different irrigation cycles (A, B, C) with custom start times, zone durations, and active days.
- **NTP Time Sync**: Automatically keeps time updated from the internet.
- **TFT Display**: A clear color display shows system status and menus.
- **8-Channel Control**: Manages a pump and up to seven irrigation zones.
- **Fully Offline Mode**: Operates completely without a WiFi connection, with all features accessible via the device interface.

## Screenshots

### Web Interface

![image](https://github.com/user-attachments/assets/95d74a90-c597-4952-88c8-14035d9b3d4a)


### On-Device UI

![Screenshot from 2025-06-29 21-27-22](https://github.com/user-attachments/assets/f2cf33d0-eba3-4ca9-87fd-d835c4b20bc7)


## Hardware

- **CAD Files**: [View on OnShape](https://cad.onshape.com/documents/08bfc36dcbbf2398b4b87e4d/w/2dae12507b7940b517cf4320/e/530747a0889b15aeda69d576)
- **Electronics** (sourced from [robotics.org.za](https://www.robotics.org.za/)):
  - FireBeetle 2 ESP32-C6 IoT Development Board
  - Wave 2 Inch IPS Display 320x240 SPI ST7789
  - Rotary Encoder Module - Breadboard Ready
  - 4 x 2-Channel Relay Module Opto Isolated 3.3V & 5V Logic 10A Load
  - Battery LiPo 5000mAh 3.7V
  - WCS1800 Hall 35A Current Sensor

## Firmware

The firmware is built using the Arduino framework for the ESP32. It includes a web server, a full UI for the display, and handles all the logic for scheduling and manual control.

For detailed instructions on how to set up the development environment, configure the firmware, and upload it to the ESP32, please see the [firmware README](./firmware/README.md).

## Future Work

- **Smarter Current Monitoring**: Implement logic to use the WCS1800 current sensor to detect pump failures, dry running, or other anomalies, and provide alerts or shut down the system automatically.

## Gallery

Build before the mess of wires:

![image](https://github.com/user-attachments/assets/dd677304-e893-42ce-b50a-1b302cc7d4c6)

Build after the mess of wires:

![20250608_222624](https://github.com/user-attachments/assets/e1cf5acc-12de-4254-afaa-bb1e9ae71cd1)

