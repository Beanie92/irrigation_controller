#ifndef CUSTOM_CANVAS_H
#define CUSTOM_CANVAS_H

#include <Adafruit_GFX.h>

class CustomCanvas : public GFXcanvas16 {
public:
    CustomCanvas(uint16_t w, uint16_t h) : GFXcanvas16(w, h) {}

    void setRelativeCursor(int16_t dx, int16_t dy) {
        int16_t currentX = getCursorX();
        int16_t currentY = getCursorY();
        setCursor(currentX + dx, currentY + dy);
    }
};

#endif // CUSTOM_CANVAS_H
