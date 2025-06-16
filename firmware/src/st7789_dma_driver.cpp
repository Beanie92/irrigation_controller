#include "st7789_dma_driver.h"

// Store pin numbers and SPI bus
static int8_t _dc_pin;
static int8_t _cs_pin;
static int8_t _rst_pin;
static int8_t _bl_pin;
static SPIClass* _spi;

// Helper function to send a command
static void st7789_send_command(uint8_t cmd) {
    digitalWrite(_cs_pin, LOW);
    digitalWrite(_dc_pin, LOW); // Command mode
    _spi->transfer(cmd);
    digitalWrite(_cs_pin, HIGH);
}

// Helper function to send data
static void st7789_send_data(uint8_t data) {
    digitalWrite(_cs_pin, LOW);
    digitalWrite(_dc_pin, HIGH); // Data mode
    _spi->transfer(data);
    digitalWrite(_cs_pin, HIGH);
}

// Helper function to send a buffer of data
static void st7789_send_data_buffer(const uint8_t* buffer, size_t len) {
    digitalWrite(_cs_pin, LOW);
    digitalWrite(_dc_pin, HIGH); // Data mode
    _spi->transfer(const_cast<uint8_t*>(buffer), len); // ESP32 SPI.transfer can take a buffer
    digitalWrite(_cs_pin, HIGH);
}

// Set address window
static void st7789_set_addr_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;

    st7789_send_command(ST7789_CASET); // Column addr set
    st7789_send_data(x >> 8);
    st7789_send_data(x & 0xFF);
    st7789_send_data(x_end >> 8);
    st7789_send_data(x_end & 0xFF);

    st7789_send_command(ST7789_RASET); // Row addr set
    st7789_send_data(y >> 8);
    st7789_send_data(y & 0xFF);
    st7789_send_data(y_end >> 8);
    st7789_send_data(y_end & 0xFF);

    st7789_send_command(ST7789_RAMWR); // Write to RAM
}

// Initialize the display
void st7789_init_display(int8_t dc_pin, int8_t cs_pin, int8_t rst_pin, int8_t bl_pin, SPIClass* spi_bus) {
    _dc_pin = dc_pin;
    _cs_pin = cs_pin;
    _rst_pin = rst_pin;
    _bl_pin = bl_pin;
    _spi = spi_bus;

    // Initialize pins
    pinMode(_dc_pin, OUTPUT);
    pinMode(_cs_pin, OUTPUT);
    digitalWrite(_cs_pin, HIGH); // Deselect
    if (_rst_pin >= 0) {
        pinMode(_rst_pin, OUTPUT);
        digitalWrite(_rst_pin, HIGH);
        delay(50);
        digitalWrite(_rst_pin, LOW);
        delay(50);
        digitalWrite(_rst_pin, HIGH);
        delay(150);
    }
    if (_bl_pin >= 0) {
        pinMode(_bl_pin, OUTPUT);
        digitalWrite(_bl_pin, HIGH); // Turn backlight on
    }

    _spi->begin(); // Initialize SPI bus (pins are usually set by SPIClass constructor or variant)
    // For ESP32, you might need _spi->begin(SCK, MISO, MOSI, CS) if not using default HSPI/VSPI pins
    // Assuming default pins are okay for now.

    st7789_send_command(ST7789_SWRESET); // Software reset
    delay(150);

    st7789_send_command(ST7789_SLPOUT); // Sleep out
    delay(255); // Mandatory 255ms delay

    st7789_send_command(ST7789_COLMOD); // Color mode
    st7789_send_data(0x55);             // 16-bit/pixel (RGB565)
    delay(10);

    st7789_send_command(ST7789_MADCTL); // Memory Access Control
    // For 240x320 display rotated 90 degrees clockwise (like original code screen.setRotation(3))
    // MV=1 (row/col exchange), MX=0 (normal col addr), MY=0 (normal row addr)
    // For 320x240 (canvas size), this means X maps to hardware Y, Y maps to hardware X
    // Rotation 3 (landscape, USB top right): MV=1, MX=1, MY=0 -> 0xA0
    // The canvas is 320x240. The ST7789 is 240x320 native.
    // With rotation 3, the ST7789's 240 width becomes height, 320 height becomes width.
    // So we want to map canvas (0,0) to ST7789 (0,0) after rotation.
    // Original DFRobot GDL setRotation(3) likely sets MADCTL to 0xA0 or 0x60.
    // Let's use 0xA0 (MV | MX) for landscape where 0,0 of canvas is top-left.
    // This means X increases to the right (canvas width 320), Y increases downwards (canvas height 240).
    // ST7789 hardware: X is short edge (240), Y is long edge (320).
    // MADCTL_MY (Row Address Order, 0=Top to Bottom, 1=Bottom to Top)
    // MADCTL_MX (Column Address Order, 0=Left to Right, 1=Right to Left)
    // MADCTL_MV (Row/Column Exchange, 0=Normal, 1=Exchanged)
    // For rotation 3 (240 logical width, 320 logical height, but we use 320x240 canvas):
    // We want canvas X to map to hardware Y, and canvas Y to map to hardware X.
    // This is MADCTL_MV = 1.
    // If canvas (0,0) is top-left:
    //   Canvas X from 0 to 319. Hardware Y from 0 to 319. (MADCTL_MY = 0)
    //   Canvas Y from 0 to 239. Hardware X from 0 to 239. (MADCTL_MX = 0)
    // So, MADCTL = MADCTL_MV = 0x20.
    // However, common ST7789 libraries for rotation 3 (landscape, 320 wide) use MADCTL_MX | MADCTL_MV = 0x40 | 0x20 = 0x60
    // or MADCTL_MY | MADCTL_MV = 0x80 | 0x20 = 0xA0.
    // The DFRobot library with setRotation(3) results in a 320 wide, 240 high display.
    // Let's try 0xA0 (MADCTL_MY | MADCTL_MV) which is common for this orientation.
    // GFXcanvas16 stores colors as RGB.
    // Correct orientation is MY | MV (0xA0). INVON is correct.
    // User reports G/B swap with this ("yellow looks purple, blue looks green").
    // MADCTL_BGR bit (R/B swap) does not fix G/B swap.
    // So, setting MADCTL for correct orientation and RGB data.
    st7789_send_data(MADCTL_MY | MADCTL_MV); // MY=1, MV=1. (0xA0)

    st7789_send_command(ST7789_INVON); // Display Inversion On - This was confirmed correct.
    //st7789_send_command(ST7789_INVOFF); // Display Inversion Off

    st7789_send_command(ST7789_NORON);  // Normal display on
    delay(10);

    st7789_send_command(ST7789_DISPON); // Display on
    delay(100);

    // Set backlight on (already done by pinMode, but good to have a function)
    st7789_set_backlight(true);
}

// Push the canvas buffer to the display
void st7789_push_canvas(const uint16_t* buffer, int16_t w, int16_t h) {
    if (!_spi) return;

    _spi->beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0)); // Max SPI speed for ST7789
    
    st7789_set_addr_window(0, 0, w, h); // Canvas dimensions are physical dimensions after rotation

    // DMA transfer would be ideal here. For now, use SPI.transfer(buffer, len)
    // The buffer is uint16_t, SPI transfer is uint8_t.
    // We need to send w * h * 2 bytes.
    // ESP32 SPI.transfer can handle uint16_t arrays directly if properly cast.
    // However, it's safer to cast to uint8_t* and specify byte length.
    size_t buffer_size_bytes = w * h * sizeof(uint16_t);
    
    digitalWrite(_cs_pin, LOW);
    digitalWrite(_dc_pin, HIGH); // Data mode
    
    // SPI.transferBytes is not standard Arduino, but common in ESP32.
    // Standard Arduino SPI.transfer(buffer, count) sends and receives.
    // For ESP32, spi->writeBytes or spi->transfer(buffer, len) is better for transmit-only.
    // Let's use spi->writePattern for potentially faster block writes if available,
    // or fallback to byte-by-byte or small chunks if DMA is not yet implemented.
    // Based on user feedback:
    // With MADCTL=0xA0 (MY|MV), INVON, no BGR bit, no pixel swap:
    //   Yellow (1,1,0) -> Purple (1,0,1)
    //   Blue   (0,0,1) -> Green  (0,1,0)
    // This implies panel transformation T(R,G,B) = (R_disp=G_src, G_disp=B_src, B_disp=R_src) (cyclic R->G->B->R)
    // No, this is T(R,G,B) = (R_src, B_src, G_src) (G/B swap)
    //
    // With G/B pixel swap (R_src, B_src_exp, G_src_trc) sent:
    //   Green (0,1,0) is good.
    //   Blue (0,0,1) becomes Red (1,0,0).
    //   This means (R_src, G_src_appr, B_src_appr) on glass for Green (0,1,0) is (0,1,0).
    //   And for Blue (0,0,1), (0,0,1) on glass becomes Red (1,0,0). This is a final R/B swap for blue pixels.
    //
    // The panel transformation seems to be: T_panel(R_in, G_in, B_in) = (G_in, B_in, R_in)
    // To get (R_target, G_target, B_target) on screen, we need to send P_sent such that T_panel(P_sent) = P_target.
    // (G_sent, B_sent, R_sent) = (R_target, G_target, B_target)
    // So, P_sent must be (B_target, R_target, G_target)
    // R_sent = B_target
    // G_sent = R_target
    // B_sent = G_target

    uint16_t* temp_buffer = (uint16_t*)malloc(buffer_size_bytes);
    if (temp_buffer) {
        for (int i = 0; i < w * h; i++) {
            uint16_t color_src = buffer[i];
            
            uint8_t r_src_5bit = (color_src >> 11) & 0x1F;
            uint8_t g_src_6bit = (color_src >> 5) & 0x3F;
            uint8_t b_src_5bit = color_src & 0x1F;

            // P_sent components:
            uint8_t r_sent_5bit = b_src_5bit;
            uint8_t g_sent_6bit = (r_src_5bit << 1) | (r_src_5bit >> 4); // Expand 5-bit R to 6-bit G (R5 R4 R3 R2 R1 R5)
            uint8_t b_sent_5bit = (g_src_6bit >> 1);                     // Truncate 6-bit G to 5-bit B (G5 G4 G3 G2 G1)
            
            temp_buffer[i] = (r_sent_5bit << 11) | (g_sent_6bit << 5) | b_sent_5bit;
        }
        _spi->transfer(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(temp_buffer)), buffer_size_bytes);
        free(temp_buffer);
    } else {
        // Fallback or error if malloc fails - send original buffer
        _spi->transfer(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buffer)), buffer_size_bytes);
    }
    
    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();
}

// Control backlight
void st7789_set_backlight(bool on) {
    if (_bl_pin >= 0) {
        digitalWrite(_bl_pin, on ? HIGH : LOW);
    }
}
