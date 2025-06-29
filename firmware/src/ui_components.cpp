#include "ui_components.h"
#include "CustomCanvas.h" // For CustomCanvas
#include "styling.h" // For UI color theme
#include <Arduino.h>     // For sprintf, etc.


// Helper function to set up calculated metrics for a scrollable list
void setupScrollableListMetrics(ScrollableList& list, CustomCanvas& canvas) {
    // Set default background color for selection
    list.selected_bg_color = COLOR_LIST_ITEM_SELECTED_BG;
    
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
void drawScrollableList(CustomCanvas& canvas, ScrollableList& list, bool is_active) {
    // Clear the component's background area
    canvas.fillRect(list.x, list.y, list.width, list.height, COLOR_LIST_BACKGROUND);

    // Draw the title if provided
    if (list.title != nullptr) {
        canvas.setTextSize(list.title_text_size);
        canvas.setTextColor(COLOR_LIST_TITLE_TEXT);
        canvas.setCursor(list.x + 10, list.y + 10); // Small padding from top-left
        canvas.println(list.title);
    if (*list.selected_index_ptr < list.top_visible_index) {
    }

    // Adjust top_visible_index to keep selected item in view
        list.top_visible_index = *list.selected_index_ptr;
    } else if (*list.selected_index_ptr >= list.top_visible_index + list.max_items_in_view) {
        list.top_visible_index = *list.selected_index_ptr - list.max_items_in_view + 1;
    }

    // Ensure top_visible_index is within valid bounds
    int total_items = list.num_items + (list.show_back_button ? 1 : 0);
    if (list.top_visible_index < 0) list.top_visible_index = 0;
    if (total_items > list.max_items_in_view && list.top_visible_index > total_items - list.max_items_in_view) {
        list.top_visible_index = total_items - list.max_items_in_view;
    }
     if (total_items <= list.max_items_in_view) { // If all items fit, no need to scroll
        list.top_visible_index = 0;
    }

    // Draw visible menu items
    canvas.setTextSize(list.item_text_size);

    for (int i = 0; i < list.max_items_in_view; i++) {
        int current_item_index = list.top_visible_index + i;
        if (current_item_index >= total_items) break; // Don't draw past the end

        int yPos = list.list_items_area_y + (i * list.item_render_height);
        
        // Highlight selected item only if the list is active
        if (is_active && current_item_index == *list.selected_index_ptr) {
            canvas.fillRect(list.x, yPos, list.width, list.item_render_height, list.selected_bg_color);
            canvas.setTextColor(COLOR_LIST_ITEM_SELECTED_TEXT);
        } else {
            canvas.setTextColor(COLOR_LIST_ITEM_TEXT);
        }
        canvas.setCursor(list.x + 20, yPos + 5); // Small padding from top of item row
        
        // Render item text
        bool is_back_button_item = list.show_back_button && (current_item_index == list.num_items);
        if (is_back_button_item) {
            canvas.println("<- Back");
        } else if (current_item_index < list.num_items) { // Ensure index is within bounds
            if (list.format_string != nullptr && list.data_source != nullptr) {
                // Data-driven list (e.g., for zone durations)
                char buffer[50];
                // Assuming data_source is an array of uint16_t as per current usage
                uint16_t* data_array = static_cast<uint16_t*>(list.data_source);
                sprintf(buffer, list.format_string, current_item_index + 1, data_array[current_item_index]);
                canvas.println(buffer);
            } else if (list.items != nullptr) {
                // Simple string array list
                canvas.println(list.items[current_item_index]);
            }
        }
    }

    // Draw scroll indicators if not all items are visible
    if (total_items > list.max_items_in_view) {
        canvas.setTextSize(1); // Smaller text for indicators
        canvas.setTextColor(COLOR_LIST_SCROLL_INDICATOR);
        if (list.top_visible_index > 0) {
            canvas.setCursor(list.x + list.width - 20, list.list_items_area_y + 5); // Top right
            canvas.println("^");
        }
        if (list.top_visible_index + list.max_items_in_view < total_items) {
            canvas.setCursor(list.x + list.width - 20, list.y + list.height - 15); // Bottom right
            canvas.println("v");
        }
    }
}

// Helper function to handle encoder input for a scrollable list
void handleScrollableListInput(ScrollableList& list, long encoder_diff) {
    int total_items = list.num_items + (list.show_back_button ? 1 : 0);
    if (total_items == 0) return; // Nothing to do if the list is empty

    int new_selected_index = *list.selected_index_ptr + encoder_diff;

    // Implement wrap-around for selection
    if (new_selected_index < 0) {
        new_selected_index = total_items - 1;
    } else if (new_selected_index >= total_items) {
        new_selected_index = 0;
    }
    *list.selected_index_ptr = new_selected_index;

}

// -----------------------------------------------------------------------------
//                         Header Display Component
// -----------------------------------------------------------------------------
void drawHeader(CustomCanvas& canvas, int x, int y, const SystemDateTime& dt, DayOfWeek dow, String ipAddress) {
    canvas.setCursor(x, y);
    canvas.setTextColor(COLOR_DATETIME_TEXT);
    canvas.setTextSize(2);
    canvas.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_SECONDARY_BACKGROUND);

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

    // Format example: YYYY-MM-DD HH:MM (Day)
    char buf[40];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d (%s)",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dow_str
    );
    canvas.println(buf);

    // Draw IP address
    canvas.setTextSize(1);
    canvas.setCursor(x, y + 20); // Position below the date/time
    canvas.setTextColor(COLOR_TEXT_SECONDARY);
    canvas.println(ipAddress);
}

// -----------------------------------------------------------------------------
//                         General UI Helper Functions
// -----------------------------------------------------------------------------

void setRelativeCursor(CustomCanvas& canvas, int16_t dx, int16_t dy) {
    int16_t currentX = canvas.getCursorX();
    int16_t currentY = canvas.getCursorY();
    canvas.setCursor(currentX + dx, currentY + dy);
}
