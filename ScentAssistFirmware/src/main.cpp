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

//#define DEBUG true  // Uncomment to Turn On Motion Sensor Debugging Statements

/**************************** PIN DEFINITIONS *********************************/
#define MOTION_INPUT_PIN A0
#define PUSHBUTTON_INPUT_PIN 12
#define RELAY_OUTPUT_PIN 6
#define LED_OUTPUT_PIN 11

/*************************** GENERAL CONSTANTS ********************************/
#define FILTER_LENGTH 10 // Seemed Reasonable
#define MIN_THRESHOLD 20 // Determined by Experimentation

/***************************** TIME CONSTANTS *********************************/
const uint32_t c_DELAY_TIME = 300000000;          // 5 Minutes
const uint32_t c_RUN_TIME = 480000000;            // 8 Minutes
const uint32_t c_HEARTBEAT_BLINK_TIME = 5000000;  // 5 Seconds
const uint32_t c_BLOCK_DETECTION_DELAY = 3000000; // 3 Seconds
const uint32_t c_WAITING_BLINK_TIME = 100000;     // 100 Milliseconds
const uint32_t c_DETECTION_INTER_DELAY = 100000;  // 100 Milliseconds
const float c_IIR_COEF = 0.40;

/*************************** STATE ENUMERATIONS *******************************/
enum controlState {
  IDLE = 0,
  DETECTED,
  ACTIVATE,
  RESET
};

/****************************      SETUP      *********************************/
void setup() {
  Serial.begin(115200);
  Serial.println("ScentAssist STARTUP - (c) STANLEY SOLUTIONS");

  // Initialize the I/O Pins
  pinMode(MOTION_INPUT_PIN, INPUT);
  pinMode(PUSHBUTTON_INPUT_PIN, INPUT_PULLUP);
  pinMode(RELAY_OUTPUT_PIN, OUTPUT);
  pinMode(LED_OUTPUT_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Set Output Defaults
  digitalWrite(RELAY_OUTPUT_PIN, false);
  for (uint8_t i = 0; i < 10; i++) {
    digitalWrite(LED_OUTPUT_PIN, true);
    delay(100);
    digitalWrite(LED_OUTPUT_PIN, false);
    delay(100);
  }

  Serial.println("READY.");
}

uint32_t timepassed(uint32_t timeLeft, unsigned long lastTime) {
  /*******   Evaluate the difference, do not allow negative-overflow.   *******/
  uint32_t timeElapsed;

  timeElapsed = uint32_t(micros() - lastTime);

  // Evaluate Time Remaining, 0 as an absolute minimum.
  if (timeElapsed < timeLeft) {
    timeLeft -= timeElapsed;
  } else {
    timeLeft = 0;
  }

  return timeLeft;
}

bool qualifyAllBits(uint8_t val) {
  /*******             Evaluate whether all bits are set.               *******/
  uint8_t mask = (1ULL << 8) - 1;
  val &= mask;
  return val == mask;
}

bool qualifyAnalog() {
  /*******   Qualify analog input to determine motion sensor pickup.    *******/
  static uint8_t readings[FILTER_LENGTH];
  static uint8_t readingIndex;
  uint8_t sample = analogRead(MOTION_INPUT_PIN); // Collect This Sample
  uint8_t average = 0;
  bool detect;

  // Evaluate Average
  for (uint8_t i = 0; i++; i < FILTER_LENGTH) {
    average += readings[i];
  }
  average = average / FILTER_LENGTH;

  // Run Sample through Filter
  sample = uint8_t(
    (float(average) * c_IIR_COEF) + (float(sample) * (1-c_IIR_COEF))
  );

  // Load the Most Recent Sample
  readings[readingIndex] = sample;
  
  // Update Index
  if (readingIndex == FILTER_LENGTH) {
    readingIndex = 0;
  } else {
    readingIndex++;
  }

  detect = sample > (4 * max(MIN_THRESHOLD, average));

  /***************               DEBUGGING CODE               *****************/
  #ifdef DEBUG
  char buffer[255];
  sprintf(buffer, "Average: %d\t\tSample: %d\t\tResult: %d",
    average, sample, detect);
  Serial.println(buffer);
  #endif
  /****************************************************************************/
  
  // Compare Sample to Average - If Sample is > 2*average: Spike Detected
  return detect;
}

void blink(uint32_t blinkFrequency) {
  /*******   Blink the LED at a specified frequency of milliseconds.    *******/
  static unsigned long lastUSec = 0;
  static uint32_t usecRemaining = 0;

  // Deduct the milliseconds that have passed since last scan.
  usecRemaining = timepassed(usecRemaining, lastUSec);

  if (usecRemaining == 0) {
    // Change State of LED
    if (!digitalRead(LED_OUTPUT_PIN)) {
      digitalWrite(LED_OUTPUT_PIN, true);

      // Short Period
      usecRemaining = 100000; // 100 milliseconds
    } else {
      digitalWrite(LED_OUTPUT_PIN, false);

      // Reset/Update Blink Frequency
      usecRemaining = blinkFrequency;
    }
  }
  
  lastUSec = micros();
}


/****************************      EXECUTE    *********************************/
void loop() {
  static controlState state = controlState::IDLE; // Operating State of System.
  static unsigned long lastUSec = 0; // Last time which was sampled.
  static uint32_t timeRemaining = 0; // Time remaining until fan start.
  static uint32_t stopDetection = 0; // Blocking condition for motion detect.
  static uint32_t fanTimeRemain = 0; // Time remaining of fan run.
  static uint32_t blockMotionIn = 0; // Time to block motion sensor input.
  static uint32_t sampleReadTime = 0; // Time between qualifying motion samples.
  static uint8_t detectionSet = 0; // Set of detection samples.
  static bool fanRunning = false; // Control indicator that fan is running.
  controlState nextState = state; // Next state system will operate in.
  bool motionDetected = false; // Motion has been detected.
  bool manualActivate = false; // Manually activated by pushbutton.
  bool detect; // Instantaneous Motion detection.

  // Read and Qualify Motion Input
  if (blockMotionIn == 0) {
    detect = qualifyAnalog();

    if (sampleReadTime == 0) {
      detectionSet = detectionSet << 1; // Shift oldest sample off
      detectionSet |= uint8_t(detect); // Set Lowest Bit According to Detection
      sampleReadTime = c_DETECTION_INTER_DELAY;
    } else {
      sampleReadTime = timepassed(sampleReadTime, lastUSec);
    }

    motionDetected = qualifyAllBits(detectionSet);
  }

  // Read Pushbutton
  manualActivate = digitalRead(PUSHBUTTON_INPUT_PIN);

  // Indicate (internally) that Motion has been Detected
  digitalWrite(LED_BUILTIN, detect);

  // Decrement timers as needed.
  if (timeRemaining > 0) {
    // Subtract the Time-Delta, Ensuring 0 is the minimum viable time value.
    timeRemaining = timepassed(timeRemaining, lastUSec);

    // Monitor for Timer Elapse
    if (timeRemaining == 0) {
      // Move to Activate Fan, Immediately
      nextState = controlState::ACTIVATE;
    }
  }
  if (stopDetection > 0) {
    stopDetection = timepassed(stopDetection, lastUSec);
  }
  if (blockMotionIn > 0) {
    blockMotionIn = timepassed(blockMotionIn, lastUSec);
  }
  if (fanTimeRemain > 0) {
    fanTimeRemain = timepassed(fanTimeRemain, lastUSec);
  }
  lastUSec = micros(); // Update Time Reference

  // Control Blinking Behavior
  if ((!fanRunning) && (timeRemaining == 0)) {
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
      if (motionDetected && (stopDetection == 0)) {
        // Move to the Detected State
        nextState = controlState::DETECTED;
      } else if (fanRunning && manualActivate) {
        // Deactivate Fan
        nextState = controlState::RESET;
      } else if (manualActivate && !fanRunning) {
        // Move to Activate Fan, Immediately
        nextState = controlState::ACTIVATE;
      } else if ((fanTimeRemain == 0) && fanRunning) {
        // Move to Deactivate Fan
        nextState = controlState::RESET;
      }
      break;
      /**********************    END IDLE STATE    ****************************/
    }
    case controlState::DETECTED: {
      /**********************    DETECTED STATE    ****************************/
      Serial.println("State: DETECTED");
      if (fanRunning) {
        // If already running, just move to reset timer for fan runtime
        nextState = controlState::ACTIVATE;
      } else {
        // Otherwise set the countdown timer to its maximum.
        timeRemaining = c_DELAY_TIME;
        nextState = controlState::IDLE;
      }
      // Ignore Subsequent Pickups for a Delay Period
      stopDetection = c_BLOCK_DETECTION_DELAY;
      break;
      /**********************  END DETECTED STATE  ****************************/
    }
    case controlState::ACTIVATE: {
      /**********************    ACTIVATE STATE    ****************************/
      Serial.println("State: ACTIVATE");
      fanRunning = true;
      fanTimeRemain = c_RUN_TIME; // Set fan runtime to maximum
      digitalWrite(RELAY_OUTPUT_PIN, true); // Turn On
      digitalWrite(LED_OUTPUT_PIN, true);

      // Reset Time Remaining (in case of manual activation)
      timeRemaining = 0;

      nextState = controlState::IDLE;
      delay(350); // Debounce
      break;
      /**********************  END ACTIVATE STATE  ****************************/
    }
    case controlState::RESET: {
      /**********************     RESET STATE      ****************************/
      Serial.println("State: RESET");
      fanRunning = false;
      fanTimeRemain = 0;
      timeRemaining = 0;
      blockMotionIn = 5 * c_BLOCK_DETECTION_DELAY; // Block Motion Sensor Input.
      digitalWrite(RELAY_OUTPUT_PIN, false); // Turn Off
      digitalWrite(LED_OUTPUT_PIN, false);

      // Delay when manually deactivated
      if (manualActivate) {
        Serial.println("Delay for Debounce.");
        delay(c_BLOCK_DETECTION_DELAY / 1000);
        Serial.println("Delay Expired.");
      }

      nextState = controlState::IDLE;
      break;
      /**********************   END RESET STATE    ****************************/
    }
  }
  /************************ END FINITE STATE MACHINE **************************/

  // Move to Next State
  state = nextState;
}