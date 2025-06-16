#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <Adafruit_GFX.h> // For GFXcanvas16 and color definitions

// -----------------------------------------------------------------------------
//                           Data Structures
// -----------------------------------------------------------------------------
// Moved from src.ino to be shared with UI components

struct SystemDateTime {
  int year;   // e.g., 2023
  int month;  // 1..12
  int day;    // 1..31
  int hour;   // 0..23
  int minute; // 0..59
  int second; // 0..59
};

typedef enum {
    SUNDAY    = 0b00000001,
    MONDAY    = 0b00000010,
    TUESDAY   = 0b00000100,
    WEDNESDAY = 0b00001000,
    THURSDAY  = 0b00010000,
    FRIDAY    = 0b00100000,
    SATURDAY  = 0b01000000,
    EVERYDAY  = 0b01111111  // All days
} DayOfWeek;


// -----------------------------------------------------------------------------
//                           UI Components
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                           Scrollable List Component
// -----------------------------------------------------------------------------
struct ScrollableList {
    // Data Source
    const char** items;           // Array of strings for list items (for simple lists)
    void* data_source;            // Generic pointer to an array of data (e.g., int[], ProgramConfig[])
    int num_items;                // Total number of items in the list
    int* selected_index_ptr;      // Pointer to the external variable holding the current selection
    const char* format_string;    // Optional format string for displaying items (e.g., "Zone %d: %d min")

    // Display Properties
    int x, y, width, height;      // Bounding box for the entire component on screen
    int item_text_size;           // Font size for list items
    uint16_t item_text_color;
    uint16_t selected_item_text_color;
    uint16_t selected_item_bg_color; // Background color for the selected item
    uint16_t list_bg_color;          // Background color for the list area

    // Optional Title
    const char* title;
    int title_text_size;
    uint16_t title_text_color;
    int title_area_height;        // Calculated or fixed height for the title area
    bool show_back_button;

    // Calculated & State Variables (managed internally by helper functions)
    int item_render_height;       // Calculated height of a single item row (font + padding)
    int list_items_area_y;        // Starting Y for the items, after title
    int list_items_area_height;   // Height available for just the items
    int max_items_in_view;        // Max items that can be shown at once
    int top_visible_index;        // Index of the item displayed at the top of the list area
};

// Helper functions for the scrollable list
void setupScrollableListMetrics(ScrollableList& list, GFXcanvas16& canvas);
void drawScrollableList(GFXcanvas16& canvas, ScrollableList& list, bool is_active);
void handleScrollableListInput(ScrollableList& list, long encoder_diff);

// Date/Time Display Component
void drawDateTimeComponent(GFXcanvas16& canvas, int x, int y, const SystemDateTime& dt, DayOfWeek dow);


#endif // UI_COMPONENTS_H
