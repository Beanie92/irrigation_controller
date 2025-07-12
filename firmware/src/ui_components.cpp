#include "ui_components.h"
#include "CustomCanvas.h"
#include "styling.h"
#include "wifi_manager.h" // For wifi_manager_get_rssi()
#include <Arduino.h>

// Function to draw the WiFi signal strength icon
void drawWifiIcon(CustomCanvas& canvas, int x, int y, int8_t rssi) {
    const int barHeightMax = 12;
    const int barWidth = 3;
    const int barSpacing = 2;

    int num_filled_bars = 0;
    if (wifi_manager_is_connected()) {
        if (rssi >= -60) {
            num_filled_bars = 4;
        } else if (rssi >= -70) {
            num_filled_bars = 3;
        } else if (rssi >= -80) {
            num_filled_bars = 2;
        } else { // rssi < -80 but connected
            num_filled_bars = 1;
        }
    }

    // Draw 4 bars for the icon
    for (int i = 0; i < 4; i++) {
        int barHeight = (barHeightMax / 4) * (i + 1);
        int barX = x + (i * (barWidth + barSpacing));
        int barY = y + (barHeightMax - barHeight);

        if (i < num_filled_bars) {
            // This bar should be filled
            uint16_t barColor;
            if (num_filled_bars >= 3) barColor = COLOR_SUCCESS;
            else if (num_filled_bars == 2) barColor = COLOR_WARNING;
            else barColor = COLOR_ERROR;
            canvas.fillRect(barX, barY, barWidth, barHeight, barColor);
        } else {
            // This bar should be an empty outline
            canvas.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT_SECONDARY);
        }
    }
}

// Function to draw the battery icon
void drawBatteryIcon(CustomCanvas& canvas, int x, int y, int level) {
    const int iconWidth = 22;
    const int iconHeight = 12;
    const int terminalWidth = 2;
    const int terminalHeight = 6;
    const int borderWidth = 1;

    canvas.drawRect(x, y, iconWidth, iconHeight, COLOR_TEXT_SECONDARY);
    canvas.fillRect(x + iconWidth, y + (iconHeight / 2) - (terminalHeight / 2), terminalWidth, terminalHeight, COLOR_TEXT_SECONDARY);

    uint16_t fillColor;
    if (level > 50) {
        fillColor = COLOR_SUCCESS;
    } else if (level > 20) {
        fillColor = COLOR_WARNING;
    } else {
        fillColor = COLOR_ERROR;
    }

    int levelWidth = (level * (iconWidth - 2 * borderWidth)) / 100;
    if (levelWidth > 0) {
        canvas.fillRect(x + borderWidth, y + borderWidth, levelWidth, iconHeight - 2 * borderWidth, fillColor);
    }
}

void drawHeader(CustomCanvas& canvas, int x, int y, const SystemDateTime& dt, DayOfWeek dow, String ipAddress, int batteryLevel, int8_t rssi) {
    canvas.setCursor(x, y);
    canvas.setTextColor(COLOR_DATETIME_TEXT);
    canvas.setTextSize(2);
    canvas.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_SECONDARY_BACKGROUND);

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

    char buf[40];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d %s", dt.year, dt.month, dt.day, dt.hour, dt.minute, dow_str);
    canvas.println(buf);

    canvas.setTextSize(1);
    canvas.setCursor(x, y + 20);
    canvas.setTextColor(COLOR_TEXT_SECONDARY);
    canvas.println(ipAddress);

    drawBatteryIcon(canvas, SCREEN_WIDTH - 33, y , batteryLevel);
    drawWifiIcon(canvas, SCREEN_WIDTH - 60, y , rssi);
}

// Helper function to set up calculated metrics for a scrollable list
void setupScrollableListMetrics(ScrollableList& list, CustomCanvas& canvas) {
    list.selected_bg_color = COLOR_LIST_ITEM_SELECTED_BG;
    list.item_render_height = (8 * list.item_text_size) + 4;
    list.title_area_height = 0;
    if (list.title != nullptr) {
        list.title_area_height = (8 * list.title_text_size) + 10;
    }
    list.list_items_area_y = list.y + list.title_area_height;
    list.list_items_area_height = list.height - list.title_area_height;
    list.max_items_in_view = list.list_items_area_height / list.item_render_height;
    if (list.max_items_in_view < 1) list.max_items_in_view = 1;
    list.top_visible_index = 0;
}

// Helper function to draw a scrollable list
void drawScrollableList(CustomCanvas& canvas, ScrollableList& list, bool is_active) {
    canvas.fillRect(list.x, list.y, list.width, list.height, COLOR_LIST_BACKGROUND);

    if (list.title != nullptr) {
        canvas.setTextSize(list.title_text_size);
        canvas.setTextColor(COLOR_LIST_TITLE_TEXT);
        canvas.setCursor(list.x + 10, list.y + 10);
        canvas.println(list.title);
    }

    if (*list.selected_index_ptr < list.top_visible_index) {
        list.top_visible_index = *list.selected_index_ptr;
    } else if (*list.selected_index_ptr >= list.top_visible_index + list.max_items_in_view) {
        list.top_visible_index = *list.selected_index_ptr - list.max_items_in_view + 1;
    }

    int total_items = list.num_items + (list.show_back_button ? 1 : 0);
    if (list.top_visible_index < 0) list.top_visible_index = 0;
    if (total_items > list.max_items_in_view && list.top_visible_index > total_items - list.max_items_in_view) {
        list.top_visible_index = total_items - list.max_items_in_view;
    }
     if (total_items <= list.max_items_in_view) {
        list.top_visible_index = 0;
    }

    canvas.setTextSize(list.item_text_size);

    for (int i = 0; i < list.max_items_in_view; i++) {
        int current_item_index = list.top_visible_index + i;
        if (current_item_index >= total_items) break;

        int yPos = list.list_items_area_y + (i * list.item_render_height);
        
        if (is_active && current_item_index == *list.selected_index_ptr) {
            canvas.fillRect(list.x, yPos, list.width, list.item_render_height, list.selected_bg_color);
            canvas.setTextColor(COLOR_LIST_ITEM_SELECTED_TEXT);
        } else {
            canvas.setTextColor(COLOR_LIST_ITEM_TEXT);
        }
        canvas.setCursor(list.x + 20, yPos + 5);
        
        bool is_back_button_item = list.show_back_button && (current_item_index == list.num_items);
        if (is_back_button_item) {
            canvas.println("<- Back");
        } else if (current_item_index < list.num_items) {
            if (list.items != nullptr) {
                canvas.println(list.items[current_item_index]);
            }
        }
    }

    if (total_items > list.max_items_in_view) {
        canvas.setTextSize(1);
        canvas.setTextColor(COLOR_LIST_SCROLL_INDICATOR);
        if (list.top_visible_index > 0) {
            canvas.setCursor(list.x + list.width - 20, list.list_items_area_y + 5);
            canvas.println("^");
        }
        if (list.top_visible_index + list.max_items_in_view < total_items) {
            canvas.setCursor(list.x + list.width - 20, list.y + list.height - 15);
            canvas.println("v");
        }
    }
}

// Helper function to handle encoder input for a scrollable list
void handleScrollableListInput(ScrollableList& list, long encoder_diff) {
    int total_items = list.num_items + (list.show_back_button ? 1 : 0);
    if (total_items == 0) return;

    int new_selected_index = *list.selected_index_ptr + encoder_diff;

    if (new_selected_index < 0) {
        new_selected_index = total_items - 1;
    } else if (new_selected_index >= total_items) {
        new_selected_index = 0;
    }
    *list.selected_index_ptr = new_selected_index;
}

void setRelativeCursor(CustomCanvas& canvas, int16_t dx, int16_t dy) {
    int16_t currentX = canvas.getCursorX();
    int16_t currentY = canvas.getCursorY();
    canvas.setCursor(currentX + dx, currentY + dy);
}
