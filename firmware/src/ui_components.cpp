#include "ui_components.h"
#include <Arduino.h>

// Helper function to set up calculated metrics for a scrollable list
void setupScrollableListMetrics(ScrollableList& list, DFRobot_ST7789_240x320_HW_SPI& screen) {
    // Calculate item render height (text height + padding)
    // Assuming 8 pixels per text size unit for height, plus 4 pixels padding
    list.item_render_height = (8 * list.item_text_size) + 4;

    // Calculate title area height
    list.title_area_height = 0;
    if (list.title != nullptr) {
        list.title_area_height = (8 * list.title_text_size) + 10; // Title text height + padding
    }

    // Calculate list items area
    list.list_items_area_y = list.y + list.title_area_height;
    list.list_items_area_height = list.height - list.title_area_height;

    // Calculate max items that can be shown in view
    list.max_items_in_view = list.list_items_area_height / list.item_render_height;
    if (list.max_items_in_view < 1) list.max_items_in_view = 1; // Ensure at least one item can be shown

    // Initialize top visible index
    list.top_visible_index = 0;
}

// Helper function to draw a scrollable list
void drawScrollableList(DFRobot_ST7789_240x320_HW_SPI& screen, ScrollableList& list, bool is_active) {
    // Clear the component's background area
    screen.fillRect(list.x, list.y, list.width, list.height, list.list_bg_color);

    // Draw the title if provided
    if (list.title != nullptr) {
        screen.setTextSize(list.title_text_size);
        screen.setTextColor(list.title_text_color);
        screen.setCursor(list.x + 10, list.y + 10); // Small padding from top-left
        screen.println(list.title);
    }

    // Adjust top_visible_index to keep selected item in view
    if (*list.selected_index_ptr < list.top_visible_index) {
        list.top_visible_index = *list.selected_index_ptr;
    } else if (*list.selected_index_ptr >= list.top_visible_index + list.max_items_in_view) {
        list.top_visible_index = *list.selected_index_ptr - list.max_items_in_view + 1;
    }

    // Ensure top_visible_index is within valid bounds
    if (list.top_visible_index < 0) list.top_visible_index = 0;
    if (list.num_items > list.max_items_in_view && list.top_visible_index > list.num_items - list.max_items_in_view) {
        list.top_visible_index = list.num_items - list.max_items_in_view;
    }
    if (list.num_items <= list.max_items_in_view) { // If all items fit, no need to scroll
        list.top_visible_index = 0;
    }

    // Draw visible menu items
    screen.setTextSize(list.item_text_size);
    for (int i = 0; i < list.max_items_in_view; i++) {
        int current_item_index = list.top_visible_index + i;
        if (current_item_index >= list.num_items) break; // Don't draw past the end

        int yPos = list.list_items_area_y + (i * list.item_render_height);
        
        // Highlight selected item only if the list is active
        if (is_active && current_item_index == *list.selected_index_ptr) {
            screen.fillRect(list.x, yPos, list.width, list.item_render_height, list.selected_item_bg_color);
            screen.setTextColor(list.selected_item_text_color);
        } else {
            screen.setTextColor(list.item_text_color);
        }
        screen.setCursor(list.x + 10, yPos + 2); // Small padding from top of item row
        
        // Render item text
        if (list.format_string != nullptr && list.data_source != nullptr) {
            char buffer[50];
            uint16_t* durations = static_cast<uint16_t*>(list.data_source);
            sprintf(buffer, list.format_string, current_item_index + 1, durations[current_item_index]);
            screen.println(buffer);
        } else {
            screen.println(list.items[current_item_index]);
        }
    }

    // Draw scroll indicators if not all items are visible
    if (list.num_items > list.max_items_in_view) {
        screen.setTextSize(1); // Smaller text for indicators
        screen.setTextColor(COLOR_RGB565_CYAN);
        if (list.top_visible_index > 0) {
            screen.setCursor(list.x + list.width - 20, list.list_items_area_y + 5); // Top right
            screen.println("^");
        }
        if (list.top_visible_index + list.max_items_in_view < list.num_items) {
            screen.setCursor(list.x + list.width - 20, list.list_items_area_y + list.list_items_area_height - 15); // Bottom right
            screen.println("v");
        }
    }
}

// Helper function to handle encoder input for a scrollable list
void handleScrollableListInput(ScrollableList& list, long encoder_diff) {
    int new_selected_index = *list.selected_index_ptr + encoder_diff;

    // Implement wrap-around for selection
    if (new_selected_index < 0) {
        new_selected_index = list.num_items - 1;
    } else if (new_selected_index >= list.num_items) {
        new_selected_index = 0;
    }
    *list.selected_index_ptr = new_selected_index;
}

// -----------------------------------------------------------------------------
//                       Date/Time Display Component
// -----------------------------------------------------------------------------
void drawDateTimeComponent(DFRobot_ST7789_240x320_HW_SPI& screen, int x, int y, const SystemDateTime& dt, DayOfWeek dow) {
    screen.setCursor(x, y);
    screen.setTextColor(COLOR_RGB565_GREEN);
    screen.setTextSize(2);

    // Get day of the week string
    const char* dow_str = "";
    switch (dow) {
        case SUNDAY:    dow_str = "Sun"; break;
        case MONDAY:    dow_str = "Mon"; break;
        case TUESDAY:   dow_str = "Tue"; break;
        case WEDNESDAY: dow_str = "Wed"; break;
        case THURSDAY:  dow_str = "Thu"; break;
        case FRIDAY:    dow_str = "Fri"; break;
        case SATURDAY:  dow_str = "Sat"; break;
        default:        dow_str = "---"; break;
    }

    // Format example: YYYY-MM-DD HH:MM:SS (Day)
    char buf[40];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d (%s)",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dow_str
    );
    screen.println(buf);
}
