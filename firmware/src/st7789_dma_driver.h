#ifndef ST7789_DMA_DRIVER_H
#define ST7789_DMA_DRIVER_H

#include <Adafruit_ST7789.h>



void st7789_init_display(int8_t dc_pin, int8_t cs_pin, int8_t rst_pin, int8_t bl_pin, SPIClass* spi_bus);
void st7789_push_canvas(const uint16_t* buffer, int16_t w, int16_t h);
void st7789_set_backlight(bool on);

#endif // ST7789_DMA_DRIVER_H
