#include "EncoderManager.h"

// We keep track of encoder counts here:
static long lastEncoderPosition = 0;
volatile long encoderValue = 0;
volatile bool encoderMoved = false;

// For button software debounce:
unsigned long lastButtonPressTime = 0;

// -----------------------------------------------------------------------------
// Interrupt Service Routine for pinA
// -----------------------------------------------------------------------------
void IRAM_ATTR isrPinA() {
  bool A = digitalRead(pinA);
  bool B = digitalRead(pinB);
  if (A == B) {
    encoderValue--;
  } else {
    encoderValue++;
  }
  encoderMoved = true;
}

// -----------------------------------------------------------------------------
// Handle Encoder Movement
// -----------------------------------------------------------------------------
void handleEncoderMovement() {
  if (!encoderMoved) return;

  noInterrupts();
  long newVal = encoderValue;
  encoderMoved = false;
  interrupts();

  long diff = newVal - lastEncoderPosition;
  lastEncoderPosition = newVal;
  if (diff == 0) return;

  switch (currentState) {
    case STATE_MAIN_MENU:
      // Scroll through the main menu
      if (diff > 0) selectedMainMenuIndex++;
      else selectedMainMenuIndex--;
      if (selectedMainMenuIndex < 0) selectedMainMenuIndex = MAIN_MENU_ITEMS - 1;
      if (selectedMainMenuIndex >= MAIN_MENU_ITEMS) selectedMainMenuIndex = 0;
      drawMainMenu();
      break;

    case STATE_MANUAL_RUN:
      // Scroll through zones
      if (diff > 0) selectedManualZoneIndex++;
      else selectedManualZoneIndex--;
      if (selectedManualZoneIndex < 0) selectedManualZoneIndex = ZONE_COUNT - 1;
      if (selectedManualZoneIndex >= ZONE_COUNT) selectedManualZoneIndex = 0;
      drawManualRunMenu();
      break;

    case STATE_SET_SYSTEM_TIME:
      handleSetSystemTimeEncoder(diff);
      break;

    case STATE_SET_CYCLE_START:
      handleSetCycleStartEncoder(diff);
      break;

    case STATE_PROG_A:
      handleProgramEditEncoder(diff, programA, "Program A");
      break;

    case STATE_PROG_B:
      handleProgramEditEncoder(diff, programB, "Program B");
      break;

    case STATE_PROG_C:
      handleProgramEditEncoder(diff, programC, "Program C");
      break;

    case STATE_RUNNING_ZONE:
      // Typically ignore encoder in running state
      break;
  }
}

// -----------------------------------------------------------------------------
// Handle Button Press
// -----------------------------------------------------------------------------
void handleButtonPress() {
  static bool lastButtonState = HIGH;
  bool currentReading = digitalRead(button);

  if (currentReading == LOW && lastButtonState == HIGH) {
    unsigned long now = millis();
    if ((now - lastButtonPressTime) > buttonDebounce) {
      lastButtonPressTime = now;

      switch (currentState) {
        case STATE_MAIN_MENU:
          switch (selectedMainMenuIndex) {
            case 0:  // Manual Run
              enterState(STATE_MANUAL_RUN);
              break;
            case 1:  // Set System Time
              enterState(STATE_SET_SYSTEM_TIME);
              break;
            case 2:  // Set Cycle Start
              enterState(STATE_SET_CYCLE_START);
              break;
            case 3:  // Program A
              enterState(STATE_PROG_A);
              break;
            case 4:  // Program B
              enterState(STATE_PROG_B);
              break;
            case 5:  // Program C
              enterState(STATE_PROG_C);
              break;
          }
          break;

        case STATE_MANUAL_RUN:
          // Start the selected zone
          startManualZone(selectedManualZoneIndex + 1);
          break;

        case STATE_SET_SYSTEM_TIME:
          handleSetSystemTimeButton();
          break;

        case STATE_SET_CYCLE_START:
          handleSetCycleStartButton();
          break;

        case STATE_PROG_A:
          handleProgramEditButton(programA, STATE_PROG_A, "Program A");
          break;

        case STATE_PROG_B:
          handleProgramEditButton(programB, STATE_PROG_B, "Program B");
          break;

        case STATE_PROG_C:
          handleProgramEditButton(programC, STATE_PROG_C, "Program C");
          break;

        case STATE_RUNNING_ZONE:
          // Press button => cancel
          stopZone();
          enterState(STATE_MAIN_MENU);
          break;
      }
    }
  }
  lastButtonState = currentReading;
}
