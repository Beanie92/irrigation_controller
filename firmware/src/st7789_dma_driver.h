#ifndef ST7789_DMA_DRIVER_H
#define ST7789_DMA_DRIVER_H

#include <Adafruit_ST7789.h>

// Standard 16-bit (RGB565) color definitions
#define COLOR_RGB565_BLACK   0x0000
#define COLOR_RGB565_BLUE    0x001F
#define COLOR_RGB565_RED     0xF800
#define COLOR_RGB565_GREEN   0x07E0
#define COLOR_RGB565_CYAN    0x07FF
#define COLOR_RGB565_MAGENTA 0xF81F
#define COLOR_RGB565_YELLOW  0xFFE0
#define COLOR_RGB565_WHITE   0xFFFF
#define COLOR_RGB565_LGRAY   0xC618 // Light Gray
#define COLOR_RGB565_ORANGE  0xFD20 // Orange

void st7789_init_display(int8_t dc_pin, int8_t cs_pin, int8_t rst_pin, int8_t bl_pin, SPIClass* spi_bus);
void st7789_push_canvas(const uint16_t* buffer, int16_t w, int16_t h);
void st7789_set_backlight(bool on);

#endif // ST7789_DMA_DRIVER_H
