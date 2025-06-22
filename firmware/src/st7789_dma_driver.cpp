#include "st7789_dma_driver.h"
#include <SPI.h>

// Global pointer to the display object
static Adafruit_ST7789* tft = nullptr;
static int8_t _bl_pin = -1;

// Initialize the display
void st7789_init_display(int8_t dc_pin, int8_t cs_pin, int8_t rst_pin, int8_t bl_pin, SPIClass* spi_bus) {
    if (tft) {
        delete tft;
    }
    tft = new Adafruit_ST7789(spi_bus, cs_pin, dc_pin, rst_pin);
    _bl_pin = bl_pin;

    tft->init(240, 320); // Initialize with ST7789 chip resolution
    tft->setRotation(1); // Set rotation for 320x240 display
    tft->fillScreen(ST77XX_BLACK);

    if (_bl_pin >= 0) {
        pinMode(_bl_pin, OUTPUT);
        digitalWrite(_bl_pin, HIGH); // Turn backlight on
    }
}

// Push the canvas buffer to the display
void st7789_push_canvas(const uint16_t* buffer, int16_t w, int16_t h) {
    if (!tft) return;
    tft->drawRGBBitmap(0, 0, (uint16_t*)buffer, w, h);
}

// Control backlight
void st7789_set_backlight(bool on) {
    if (_bl_pin >= 0) {
        digitalWrite(_bl_pin, on ? HIGH : LOW);
    }
}
