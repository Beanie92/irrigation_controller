#include "styling.h"

#ifndef CUSTOM_CANVAS_H
#define CUSTOM_CANVAS_H

#include <Adafruit_GFX.h>

class CustomCanvas : public GFXcanvas16 {
public:
    CustomCanvas(uint16_t w, uint16_t h) : GFXcanvas16(w, h), _textSize(1) {}

    void setTextSize(uint8_t s) {
        _textSize = s;
        GFXcanvas16::setTextSize(s);
    }

    void setRelativeCursor(int16_t dx, int16_t dy) {
        int16_t currentX = getCursorX();
        int16_t currentY = getCursorY();
        setCursor(currentX + dx, currentY + dy);
    }
    void setNewLine(int16_t y_padding = 0) {
        int16_t currentY = getCursorY();
        int16_t textHeight = 8 * _textSize; // Font size is 8 pixels high
        const int16_t textPadding = 3;
        setCursor(LEFT_PADDING, currentY + textHeight + textPadding + y_padding);
    }
private:
    uint8_t _textSize;
};

#endif // CUSTOM_CANVAS_H
