#include "display.h"

Display::Display() : screen(TFT_DC, TFT_CS, TFT_RST) {}

void Display::begin() {
    screen.begin();
    screen.fillScreen(COLOR_RGB565_BLACK);
}

void Display::drawDateTime(int x, int y, const SystemDateTime& dt) {
    screen.setCursor(x, y);
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setTextSize(2);

    char buf[32];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
            dt.year, dt.month, dt.day,
            dt.hour, dt.minute, dt.second);
    screen.println(buf);
}

void Display::clearScreen() {
    screen.fillScreen(COLOR_RGB565_BLACK);
}

void Display::drawText(int x, int y, const char* text, uint16_t color, uint8_t size) {
    screen.setTextColor(color);
    screen.setTextSize(size);
    screen.setCursor(x, y);
    screen.println(text);
}