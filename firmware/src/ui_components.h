#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include "CustomCanvas.h" // For CustomCanvas, which includes Adafruit_GFX.h

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

// Number of irrigation zones (excluding the pump)
#define ZONE_COUNT 7

// Time structure for cycle start times
typedef struct {
    uint8_t hour;    // 0-23
    uint8_t minute;  // 0-59
} TimeOfDay;

// Main cycle configuration structure
struct CycleConfig {
    bool enabled;           // Whether this cycle is active
    TimeOfDay startTime;    // When to start the cycle
    uint8_t daysActive;     // Bitfield using DayOfWeek values
    uint8_t interZoneDelay; // Minutes to wait between zones
    uint16_t zoneDurations[ZONE_COUNT]; // Minutes per zone
    char name[16];          // Optional: Cycle name/description
};

// Enum for tracking the type of active operation
typedef enum {
  OP_NONE,
  OP_MANUAL_ZONE,
  OP_MANUAL_CYCLE,
  OP_SCHEDULED_CYCLE
} ActiveOperationType;


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
    uint16_t selected_bg_color;   // Background color for the selected item

    // Optional Title
    const char* title;
    int title_text_size;
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
void setupScrollableListMetrics(ScrollableList& list, CustomCanvas& canvas);
void drawScrollableList(CustomCanvas& canvas, ScrollableList& list, bool is_active);
void handleScrollableListInput(ScrollableList& list, long encoder_diff);

// Header Display Component
void drawHeader(CustomCanvas& canvas, int x, int y, const SystemDateTime& dt, DayOfWeek dow, String ipAddress, int batteryLevel, int8_t rssi);
void drawWifiIcon(CustomCanvas& canvas, int x, int y, int8_t rssi);

// General UI Helpers
void setRelativeCursor(CustomCanvas& canvas, int16_t dx, int16_t dy);


#endif // UI_COMPONENTS_H
