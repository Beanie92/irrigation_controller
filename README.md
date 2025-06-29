# Irrigation Controller

![image](https://github.com/user-attachments/assets/09214a03-ba51-4387-acdd-ce2d5f2400e1)

An ESP32-C6 based irrigation controller with a web interface, manual controls, and flexible scheduling.

## Features

- **WiFi & Web UI**: Configure and control the system from any web browser.
- **Manual Control**: Use the physical rotary encoder for on-the-spot adjustments.
- **Flexible Scheduling**: Set up to three different irrigation cycles (A, B, C) with custom start times, zone durations, and active days.
- **NTP Time Sync**: Automatically keeps time updated from the internet.
- **TFT Display**: A clear color display shows system status and menus.
- **8-Channel Control**: Manages a pump and up to seven irrigation zones.

## Hardware

- **CAD Files**: [View on OnShape](https://cad.onshape.com/documents/08bfc36dcbbf2398b4b87e4d/w/2dae12507b7940b517cf4320/e/530747a0889b15aeda69d576)
- **Electronics**:
  - ESP32-C6 Development Board (DFRobot Beetle)
  - ST7789 240x320 TFT Display
  - KY-040 Rotary Encoder
  - 4 *2-Channel Relay Module

## Firmware

The firmware is built using the Arduino framework for the ESP32. It includes a web server, a full UI for the display, and handles all the logic for scheduling and manual control.

For detailed instructions on how to set up the development environment, configure the firmware, and upload it to the ESP32, please see the [firmware README](./firmware/README.md).

## Gallery

Build before the mess of wires:

![image](https://github.com/user-attachments/assets/dd677304-e893-42ce-b50a-1b302cc7d4c6)

Build after the mess of wires:

![20250608_222624](https://github.com/user-attachments/assets/e1cf5acc-12de-4254-afaa-bb1e9ae71cd1)
