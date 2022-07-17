/*******************************************************************************
 * ScentAssist
 * 
 * LICENSE: MIT
 * 
 * AUTHOR: Joe Stanley - Stanley Solutions
 * 
 * ABOUT: Automatic motion-sensor activated motor controller used in conjunction
 *        with an fan to exhaust "gasses" from a feline closet and help keep
 *        freshness.
 ******************************************************************************/

#include <Arduino.h>

/**************************** PIN DEFINITIONS *********************************/
#define MOTION_INPUT_PIN 0
#define PUSHBUTTON_INPUT_PIN 0
#define RELAY_OUTPUT_PIN 0
#define LED_OUTPUT_PIN 0
#define EXTRA_GND_PIN 0

/***************************** TIME CONSTANTS *********************************/
const uint32_t c_DELAY_TIME = 5 * 60 * 1000;       // 5 Minutes
const uint32_t c_RUN_TIME = 2 * 60 * 1000;         // 2 Minutes
const uint32_t c_HEARTBEAT_BLINK_TIME = 5 * 1000;  // 5 Seconds
const uint32_t c_WAITING_BLINK_TIME = 100;         // 100 Milliseconds

/*************************** STATE ENUMERATIONS *******************************/
enum controlState {
  IDLE = 0,
  DETECTED,
  ACTIVATE,
  RESET
};

/****************************      SETUP      *********************************/
void setup() {
  // Initialize the I/O Pins
  pinMode(MOTION_INPUT_PIN, INPUT);
  pinMode(PUSHBUTTON_INPUT_PIN, INPUT);
  pinMode(RELAY_OUTPUT_PIN, OUTPUT);
  pinMode(LED_OUTPUT_PIN, OUTPUT);
  pinMode(EXTRA_GND_PIN, OUTPUT);

  // Set GND PIN
  digitalWrite(EXTRA_GND_PIN, false);

  // Set Output Defaults
  digitalWrite(RELAY_OUTPUT_PIN, false);
  digitalWrite(LED_OUTPUT_PIN, false);
}

uint32_t timepassed(uint32_t timeLeft, unsigned long lastTime) {
  /*******   Evaluate the difference, do not allow negative-overflow.   *******/
  uint32_t timeElapsed;

  timeElapsed = uint32_t(millis() - lastTime);

  // Evaluate Time Remaining, 0 as an absolute minimum.
  if (timeElapsed < timeLeft) {
    timeLeft -= timeElapsed;
  } else {
    timeLeft = 0;
  }

  return timeLeft;
}

void blink(uint32_t blinkFrequency) {
  /*******   Blink the LED at a specified frequency of milliseconds.    *******/
  static unsigned long lastTime = 0;
  static uint32_t msecRemaining = 0;

  // Deduct the milliseconds that have passed since last scan.
  msecRemaining = timepassed(msecRemaining, lastTime);

  if (msecRemaining == 0) {
    // Change State of LED
    digitalWrite(LED_OUTPUT_PIN, !digitalRead(LED_OUTPUT_PIN));

    // Reset/Update Blink Frequency
    msecRemaining = blinkFrequency;
  }
}


/****************************      EXECUTE    *********************************/
void loop() {
  controlState state = controlState::IDLE; // Operating State of System.
  controlState nextState = state; // Next state system will operate in.
  unsigned long lastTime = 0; // Last time which was sampled.
  uint32_t timeRemaining = 0; // Time remaining until fan start.
  uint32_t fanTimeRemain = 0; // Time remaining of fan run.
  uint32_t tempTime; // Temporary variable to represent time delta.
  bool motionDetected = false; // Motion has been detected, and fan should run.
  bool manualActivate = false; // Manually activated by pushbutton.
  bool fanRunning = false; // Control indicator that fan is running.

  // Decrement timers as needed.
  if (timeRemaining > 0) {
    // Subtract the Time-Delta, Ensuring 0 is the minimum viable time value.
    timeRemaining = timepassed(timeRemaining, lastTime);
  }
  if (fanTimeRemain > 0) {
    fanTimeRemain = timepassed(fanTimeRemain, lastTime);
  }
  lastTime = millis(); // Update Time Reference

  // Read Inputs
  motionDetected = digitalRead(MOTION_INPUT_PIN);
  manualActivate = digitalRead(PUSHBUTTON_INPUT_PIN);

  // Control Blinking Behavior
  if (!fanRunning && (timeRemaining == 0)) {
    // Perform Heartbeat Blink
    blink(c_HEARTBEAT_BLINK_TIME);
  } else if (timeRemaining > 0) {
    // Perform Waiting Blink
    blink(c_WAITING_BLINK_TIME);
  }

  /************************** FINITE STATE MACHINE ****************************/
  switch (state) {
    case controlState::IDLE: {
      /**********************      IDLE STATE      ****************************/
      if (motionDetected) {
        // Move to the Detected State
        nextState = controlState::DETECTED;
      } else if (manualActivate || (timeRemaining == 0)) {
        // Move to Activate Fan, Immediately
        nextState = controlState::ACTIVATE;
      } else if (fanTimeRemain == 0) {
        // Move to Deactivate Fan
        nextState = controlState::RESET;
      }
      break;
      /**********************    END IDLE STATE    ****************************/
    }
    case controlState::DETECTED: {
      /**********************    DETECTED STATE    ****************************/
      if (fanRunning) {
        // If already running, just move to reset timer for fan runtime
        nextState = controlState::ACTIVATE;
      } else {
        // Otherwise set the countdown timer to its maximum.
        timeRemaining = c_DELAY_TIME;
      }
      break;
      /**********************  END DETECTED STATE  ****************************/
    }
    case controlState::ACTIVATE: {
      /**********************    ACTIVATE STATE    ****************************/
      digitalWrite(RELAY_OUTPUT_PIN, true); // Turn On
      digitalWrite(LED_OUTPUT_PIN, true);
      fanTimeRemain = c_RUN_TIME; // Set fan runtime to maximum

      // Reset Time Remaining (in case of manual activation)
      timeRemaining = 0;
      break;
      /**********************  END ACTIVATE STATE  ****************************/
    }
    case controlState::RESET: {
      /**********************     RESET STATE      ****************************/
      digitalWrite(RELAY_OUTPUT_PIN, false); // Turn Off
      digitalWrite(LED_OUTPUT_PIN, false);
      break;
      /**********************   END RESET STATE    ****************************/
    }
  }
  /************************ END FINITE STATE MACHINE **************************/

  // Move to Next State
  state = nextState;
}