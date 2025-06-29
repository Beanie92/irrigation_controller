#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

static const char *TAG = "irrigation_controller";

// -----------------------------------------------------------------------------
//                    Rotary Encoder Inputs / Global Variables
// -----------------------------------------------------------------------------
#define PIN_A GPIO_NUM_4    // KY-040 CLK
#define PIN_B GPIO_NUM_7    // KY-040 DT
#define BUTTON_PIN GPIO_NUM_16 // KY-040 SW (with internal pull-up)

volatile long encoderValue = 0;
volatile bool encoderMoved = false;

// For button software debounce:
uint64_t lastButtonPressTime = 0;
const uint64_t buttonDebounce = 200000; // microseconds

// -----------------------------------------------------------------------------
//                        Relay Pins / Configuration
// -----------------------------------------------------------------------------
#define NUM_RELAYS 8
// Relay 0 is dedicated to the borehole pump;
// Relays 1..7 are the irrigation zones.
static const gpio_num_t relayPins[NUM_RELAYS] = {
    GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_17, GPIO_NUM_18, 
    GPIO_NUM_15, GPIO_NUM_21, GPIO_NUM_1, GPIO_NUM_14
};
bool relayStates[NUM_RELAYS] = {false, false, false, false, false, false, false, false};

#define PUMP_IDX 0   // borehole pump
#define ZONE_COUNT 7 // zones 1..7

// -----------------------------------------------------------------------------
//                           Display Pins / Driver
// -----------------------------------------------------------------------------
#define TFT_DC GPIO_NUM_2
#define TFT_CS GPIO_NUM_6
#define TFT_RST GPIO_NUM_3

// Relay labels (index 0 is the pump)
const char *relayLabels[NUM_RELAYS] = {
    "Pump (auto)", // index 0; not displayed in manual-run menu
    "Zone 1",
    "Zone 2",
    "Zone 3",
    "Zone 4",
    "Zone 5",
    "Zone 6",
    "Zone 7"};

// -----------------------------------------------------------------------------
//                           Menu and Program States
// -----------------------------------------------------------------------------
typedef enum {
  STATE_MAIN_MENU,
  STATE_MANUAL_RUN,
  STATE_SET_SYSTEM_TIME,
  STATE_SET_CYCLE_START,
  STATE_PROG_A,
  STATE_PROG_B,
  STATE_PROG_C,
  STATE_RUNNING_ZONE
} ProgramState;

ProgramState currentState = STATE_MAIN_MENU;

// Main Menu Items
#define MAIN_MENU_ITEMS 6
const char *mainMenuLabels[MAIN_MENU_ITEMS] = {
    "Manual Run",
    "Set System Time",
    "Set Cycle Start",
    "Program A",
    "Program B",
    "Program C"};
int selectedMainMenuIndex = 0;

// Manual Run zone index: 0..6 => zone = index+1
int selectedManualZoneIndex = 0;

// -----------------------------------------------------------------------------
//                  Time-Keeping (Software Simulation)
// -----------------------------------------------------------------------------
typedef struct {
  int year;   // e.g., 2023
  int month;  // 1..12
  int day;    // 1..31
  int hour;   // 0..23
  int minute; // 0..59
  int second; // 0..59
} SystemDateTime;

SystemDateTime currentDateTime = {2023, 1, 1, 8, 0, 0}; // Example start date/time
uint64_t lastSecondUpdate = 0;                     // track esp_timer_get_time() to increment seconds

void incrementOneSecond()
{
  // Very simplistic approach: just add 1 second, then handle minute/hour/day wrap
  currentDateTime.second++;
  if (currentDateTime.second >= 60)
  {
    currentDateTime.second = 0;
    currentDateTime.minute++;
    if (currentDateTime.minute >= 60)
    {
      currentDateTime.minute = 0;
      currentDateTime.hour++;
      if (currentDateTime.hour >= 24)
      {
        currentDateTime.hour = 0;
        currentDateTime.day++;
        // Very naive day wrap (assumes 30 days). In real code, handle months properly.
        if (currentDateTime.day > 30)
        {
          currentDateTime.day = 1;
          currentDateTime.month++;
          if (currentDateTime.month > 12)
          {
            currentDateTime.month = 1;
            currentDateTime.year++;
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
//                 Cycle Start Time & Program Config
// -----------------------------------------------------------------------------
typedef enum
{
  SUNDAY = 0b00000001,
  MONDAY = 0b00000010,
  TUESDAY = 0b00000100,
  WEDNESDAY = 0b00001000,
  THURSDAY = 0b00010000,
  FRIDAY = 0b00100000,
  SATURDAY = 0b01000000,
  EVERYDAY = 0b01111111 // All days
} DayOfWeek;

// Time structure (could use instead of SystemDateTime for start time)
typedef struct
{
  uint8_t hour;   // 0-23
  uint8_t minute; // 0-59
} TimeOfDay;

// Main program configuration structure
typedef struct
{
  bool enabled;                      // Whether this program is active
  TimeOfDay startTime;               // When to start the program
  uint8_t daysActive;                // Bitfield using DayOfWeek values
  uint8_t interZoneDelay;            // Minutes to wait between zones
  uint16_t zoneRunTimes[ZONE_COUNT]; // Minutes per zone
  char name[16];                     // Optional: Program name/description
} ProgramConfig;

ProgramConfig programA = {
    true,                                    // enabled
    {6, 0},                                 // startTime (6:00 AM)
    MONDAY | WEDNESDAY | FRIDAY,            // daysActive
    1,                                      // interZoneDelay
    {5, 5, 5, 5, 5, 5, 5},                 // zoneRunTimes
    "Program A"                             // name
};

// Single "cycleStartTime" for demonstration
SystemDateTime cycleStartTime = {2023, 1, 1, 6, 0, 0}; // e.g. run at 06:00 each day

// -----------------------------------------------------------------------------
//                Sub-indexes and helpers for editing fields
// -----------------------------------------------------------------------------
static int timeEditFieldIndex = 0;   // 0=year,1=month,2=day,3=hour,4=minute,5=second
static int cycleEditFieldIndex = 0;  // 0=hour,1=minute
static int programEditZoneIndex = 0; // 0..7 => 0..6=zone durations, 7=interZoneDelay

// Ranges for system time fields (simple example)
#define MIN_YEAR 2020
#define MAX_YEAR 2050

// -----------------------------------------------------------------------------
//                           Forward Declarations
// -----------------------------------------------------------------------------
static void IRAM_ATTR gpio_isr_handler(void* arg);
void handleEncoderMovement();
void handleButtonPress();

void updateSoftwareClock();

// Manual Run
void startManualZone(int zoneIdx);
void stopZone();

// GPIO initialization
void init_gpio();

// Main task
void main_task(void *pvParameters);

// -----------------------------------------------------------------------------
//                                GPIO SETUP
// -----------------------------------------------------------------------------
void init_gpio()
{
    // Configure rotary encoder pins
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_A) | (1ULL << PIN_B);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // Configure button pin
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
    gpio_config(&io_conf);

    // Configure relay pins
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    for (int i = 0; i < NUM_RELAYS; i++) {
        io_conf.pin_bit_mask = (1ULL << relayPins[i]);
        gpio_config(&io_conf);
        gpio_set_level(relayPins[i], 0);
        relayStates[i] = false;
    }

    // Install gpio isr service
    gpio_install_isr_service(0);
    
    // Hook isr handler for specific gpio pins
    gpio_isr_handler_add(PIN_A, gpio_isr_handler, (void*) PIN_A);
    gpio_isr_handler_add(BUTTON_PIN, gpio_isr_handler, (void*) BUTTON_PIN);
}

// -----------------------------------------------------------------------------
//                        INTERRUPT SERVICE ROUTINE
// -----------------------------------------------------------------------------
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    
    if (gpio_num == PIN_A) {
        bool A = gpio_get_level(PIN_A);
        bool B = gpio_get_level(PIN_B);
        if (A == B) {
            encoderValue--;
        } else {
            encoderValue++;
        }
        encoderMoved = true;
    }
}

// -----------------------------------------------------------------------------
//                         ENCODER & BUTTON HANDLERS
// -----------------------------------------------------------------------------
static long lastEncoderPosition = 0;
void handleEncoderMovement()
{
  if (!encoderMoved)
    return;

  long newVal = encoderValue;
  encoderMoved = false;

  long diff = newVal - lastEncoderPosition;
  lastEncoderPosition = newVal;
  if (diff == 0)
    return;

  switch (currentState)
  {
  case STATE_MAIN_MENU:
    // Slide through the 6 items
    if (diff > 0)
      selectedMainMenuIndex++;
    else
      selectedMainMenuIndex--;
    if (selectedMainMenuIndex < 0)
      selectedMainMenuIndex = MAIN_MENU_ITEMS - 1;
    else if (selectedMainMenuIndex >= MAIN_MENU_ITEMS)
      selectedMainMenuIndex = 0;
    ESP_LOGI(TAG, "Main menu index: %d", selectedMainMenuIndex);
    break;

  case STATE_MANUAL_RUN:
    // Slide through zones 1..7
    if (diff > 0)
      selectedManualZoneIndex++;
    else
      selectedManualZoneIndex--;
    if (selectedManualZoneIndex < 0)
      selectedManualZoneIndex = ZONE_COUNT - 1;
    else if (selectedManualZoneIndex >= ZONE_COUNT)
      selectedManualZoneIndex = 0;
    ESP_LOGI(TAG, "Manual zone index: %d", selectedManualZoneIndex);
    break;

  default:
    break;
  }
}

void handleButtonPress()
{
    static bool lastButtonState = true;
    bool currentReading = gpio_get_level(BUTTON_PIN);

    if (currentReading == false && lastButtonState == true)
    {
        uint64_t now = esp_timer_get_time();
        if ((now - lastButtonPressTime) > buttonDebounce)
        {
            lastButtonPressTime = now;

            // State-Specific Handling
            switch (currentState)
            {
            case STATE_MAIN_MENU:
                // User selected an item. Jump to that state:
                switch (selectedMainMenuIndex)
                {
                case 0: // Manual Run
                    currentState = STATE_MANUAL_RUN;
                    ESP_LOGI(TAG, "Entering Manual Run mode");
                    break;
                case 1: // Set System Time
                    currentState = STATE_SET_SYSTEM_TIME;
                    ESP_LOGI(TAG, "Entering Set System Time mode");
                    break;
                default:
                    ESP_LOGI(TAG, "Menu item %d selected", selectedMainMenuIndex);
                    break;
                }
                break;

            case STATE_MANUAL_RUN:
                // Pressing button => Start the selected zone
                startManualZone(selectedManualZoneIndex + 1); // zoneIdx 1..7
                break;

            case STATE_RUNNING_ZONE:
                // Pressing button => Cancel the running zone
                stopZone();
                currentState = STATE_MAIN_MENU;
                ESP_LOGI(TAG, "Returning to main menu");
                break;

            default:
                break;
            }
        }
    }
    lastButtonState = currentReading;
}

// -----------------------------------------------------------------------------
//                         SIMPLE SOFTWARE CLOCK
// -----------------------------------------------------------------------------
void updateSoftwareClock()
{
  uint64_t now = esp_timer_get_time();
  if ((now - lastSecondUpdate) >= 1000000) // 1 second in microseconds
  {
    lastSecondUpdate = now;
    incrementOneSecond();
    
    // Log time update occasionally
    static int logCounter = 0;
    if (++logCounter >= 60) { // Log every minute
        ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
                currentDateTime.year, currentDateTime.month, currentDateTime.day,
                currentDateTime.hour, currentDateTime.minute, currentDateTime.second);
        logCounter = 0;
    }
  }
}

// -----------------------------------------------------------------------------
//                           MANUAL RUN FUNCTIONS
// -----------------------------------------------------------------------------
void startManualZone(int zoneIdx)
{
  ESP_LOGI(TAG, "Manual Start: Zone %d", zoneIdx);

  // Turn off any previously running zone
  stopZone();

  // Switch ON the zone
  relayStates[zoneIdx] = true;
  gpio_set_level(relayPins[zoneIdx], 1);

  // Switch ON the pump
  relayStates[PUMP_IDX] = true;
  gpio_set_level(relayPins[PUMP_IDX], 1);

  // Move to "running zone" state
  currentState = STATE_RUNNING_ZONE;
}

void stopZone()
{
  // Turn off all zones
  for (int i = 1; i < NUM_RELAYS; i++)
  {
    relayStates[i] = false;
    gpio_set_level(relayPins[i], 0);
  }
  // Turn off pump
  relayStates[PUMP_IDX] = false;
  gpio_set_level(relayPins[PUMP_IDX], 0);
  
  ESP_LOGI(TAG, "All zones and pump stopped");
}

// -----------------------------------------------------------------------------
//                                MAIN TASK
// -----------------------------------------------------------------------------
void main_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Irrigation Controller Started");
    
    // Initialize GPIO
    init_gpio();
    
    ESP_LOGI(TAG, "GPIO initialized, entering main loop");
    
    while (1) {
        updateSoftwareClock();
        handleEncoderMovement();
        handleButtonPress();
        
        // Additional logic for running states if needed
        if (currentState == STATE_RUNNING_ZONE)
        {
            // e.g., check time-based zone run or cancellation
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog issues
    }
}

// -----------------------------------------------------------------------------
//                                APP MAIN
// -----------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Irrigation Controller Firmware");
    
    // Create main task
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
}
