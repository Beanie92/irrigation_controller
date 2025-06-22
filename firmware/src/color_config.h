#ifndef COLOR_CONFIG_H
#define COLOR_CONFIG_H

#include "st7789_dma_driver.h" // For the raw color definitions

// -----------------------------------------------------------------------------
//                       Additional Color Definitions
// -----------------------------------------------------------------------------
// These are extra colors for the themes that are not in the display driver.
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
#define COLOR_RGB565_DARKGREEN   0x0320
#define COLOR_RGB565_BROWN       0xA145
#define COLOR_RGB565_LIME        0xAD55
#define COLOR_RGB565_NAVY        0x0010
#define COLOR_RGB565_LIGHTBLUE   0xADDD
#define COLOR_RGB565_GREENYELLOW 0xAFE5
#define COLOR_RGB565_MAROON      0x7800
#define COLOR_RGB565_PURPLE      0x780F
#define COLOR_RGB565_LIGHTGREY   0xD69A


// -----------------------------------------------------------------------------
//                           UI Color Theme Definitions
// -----------------------------------------------------------------------------
// It's recommended to use these semantic color names in the UI code
// rather than raw color values. This makes it easier to change the
// theme later.
//
// To change the theme, comment out the active theme and uncomment the
// one you want to use.

// // --- Theme 1: Default ---
// #define COLOR_BACKGROUND      COLOR_RGB565_BLACK
// #define COLOR_TEXT_PRIMARY    COLOR_RGB565_WHITE
// #define COLOR_TEXT_SECONDARY  COLOR_RGB565_LGRAY
// #define COLOR_ACCENT_PRIMARY  COLOR_RGB565_CYAN
// #define COLOR_ACCENT_SECONDARY COLOR_RGB565_ORANGE
// #define COLOR_SUCCESS         COLOR_RGB565_GREEN
// #define COLOR_WARNING         COLOR_RGB565_YELLOW
// #define COLOR_ERROR           COLOR_RGB565_RED


/*
// --- Theme 2: Forest ---
#define COLOR_BACKGROUND      COLOR_RGB565_DARKGREEN
#define COLOR_TEXT_PRIMARY    COLOR_RGB565_WHITE
#define COLOR_TEXT_SECONDARY  COLOR_RGB565_LIGHTGREY
#define COLOR_ACCENT_PRIMARY  COLOR_RGB565_GREEN
#define COLOR_ACCENT_SECONDARY COLOR_RGB565_BROWN
#define COLOR_SUCCESS         COLOR_RGB565_LIME
#define COLOR_WARNING         COLOR_RGB565_YELLOW
#define COLOR_ERROR           COLOR_RGB565_RED
*/


// --- Theme 3: Ocean ---
#define COLOR_BACKGROUND      COLOR_RGB565_NAVY
#define COLOR_TEXT_PRIMARY    COLOR_RGB565_WHITE
#define COLOR_TEXT_SECONDARY  COLOR_RGB565_CYAN
#define COLOR_ACCENT_PRIMARY  COLOR_RGB565_BLUE
#define COLOR_ACCENT_SECONDARY COLOR_RGB565_LIGHTBLUE
#define COLOR_SUCCESS         COLOR_RGB565_GREENYELLOW
#define COLOR_WARNING         COLOR_RGB565_YELLOW
#define COLOR_ERROR           COLOR_RGB565_RED


/*
// --- Theme 4: Sunset ---
#define COLOR_BACKGROUND      COLOR_RGB565_MAROON
#define COLOR_TEXT_PRIMARY    COLOR_RGB565_WHITE
#define COLOR_TEXT_SECONDARY  COLOR_RGB565_LIGHTGREY
#define COLOR_ACCENT_PRIMARY  COLOR_RGB565_ORANGE
#define COLOR_ACCENT_SECONDARY COLOR_RGB565_PURPLE
#define COLOR_SUCCESS         COLOR_RGB565_GREEN
#define COLOR_WARNING         COLOR_RGB565_YELLOW
#define COLOR_ERROR           COLOR_RGB565_RED
*/


// -----------------------------------------------------------------------------
//                         Component-Specific Colors
// -----------------------------------------------------------------------------
// These colors are derived from the main theme colors above.

// --- Scrollable List ---
#define COLOR_LIST_BACKGROUND         COLOR_BACKGROUND
#define COLOR_LIST_ITEM_TEXT          COLOR_TEXT_PRIMARY
#define COLOR_LIST_ITEM_SELECTED_BG   COLOR_ACCENT_PRIMARY
#define COLOR_LIST_ITEM_SELECTED_TEXT (COLOR_BACKGROUND == COLOR_RGB565_BLACK ? COLOR_RGB565_BLACK : COLOR_RGB565_WHITE)
#define COLOR_LIST_TITLE_TEXT         COLOR_ACCENT_SECONDARY
#define COLOR_LIST_SCROLL_INDICATOR   COLOR_ACCENT_PRIMARY

// --- Date/Time Component ---
#define COLOR_DATETIME_TEXT           COLOR_SUCCESS

#endif // COLOR_CONFIG_H
