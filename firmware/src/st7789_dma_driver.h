#ifndef ST7789_DMA_DRIVER_H
#define ST7789_DMA_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

// Define ST7789 commands
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_RDDID   0x04
#define ST7789_RDDST   0x09

#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_PTLON   0x12
#define ST7789_NORON   0x13

#define ST7789_INVOFF  0x20
#define ST7789_INVON   0x21
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_RAMRD   0x2E

#define ST7789_PTLAR   0x30
#define ST7789_COLMOD  0x3A
#define ST7789_MADCTL  0x36

#define ST7789_FRMCTR1 0xB1
#define ST7789_FRMCTR2 0xB2
#define ST7789_FRMCTR3 0xB3
#define ST7789_INVCTR  0xB4
#define ST7789_DISSET5 0xB6

#define ST7789_GCTRL   0xB7
#define ST7789_GTADJ   0xB8
#define ST7789_VCOMS   0xBB

#define ST7789_LCMCTRL 0xC0
#define ST7789_IDSET   0xC1
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS    0xC3
#define ST7789_VDVS    0xC4
#define ST7789_VMCTR1  0xC5
#define ST7789_FRCTRL2 0xC6
#define ST7789_CABCCTRL 0xC7

#define ST7789_PWCTRL1 0xD0
#define ST7789_PWCTRL2 0xD1 
#define ST7789_RDID1   0xDA
#define ST7789_RDID2   0xDB
#define ST7789_RDID3   0xDC
#define ST7789_RDID4   0xDD

#define ST7789_GMCTRP1 0xE0
#define ST7789_GMCTRN1 0xE1

// MADCTL Bits
#define MADCTL_MY  0x80  // Row address order
#define MADCTL_MX  0x40  // Column address order
#define MADCTL_MV  0x20  // Row/Column exchange
#define MADCTL_ML  0x10  // Vertical refresh order
#define MADCTL_RGB 0x00  // RGB order
#define MADCTL_BGR 0x08  // BGR order
#define MADCTL_MH  0x04  // Horizontal refresh order

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

// Function declarations
void st7789_init_display(int8_t dc_pin, int8_t cs_pin, int8_t rst_pin, int8_t bl_pin, SPIClass* spi_bus);
void st7789_push_canvas(const uint16_t* buffer, int16_t w, int16_t h);
void st7789_set_backlight(bool on);

#endif // ST7789_DMA_DRIVER_H
