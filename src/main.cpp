#include <Arduino.h>
#include <pico/rand.h>
#include <graphics.h>
#include <counter.h>
#include <serialutils.h>
#include <ringindicator.h>
#include <interpolator.h>
#include <SoftwareSerial.h>
#include <QMI8658.h>
#include <display.h>
#include <levelsensor.h>
#include <statusindicator.h>
#include <greetingdisplay.h>
#include <config.h>
#include <easing.h>
#include <perf.h>

// UI elements
Counter counter;
Interpolator interpolator;
RingIndicator ring;
StatusIndicator status;
GreetingDisplay greeting;

// Initial ring indicator color
uint16_t currRingColor = RING_COLOR_OK;
// Fill level percentage tracker
float currPerc, targetPerc = 0;
// Sleep states
bool isAwake, isVibrating, isVibratingPrev = false;
// Timing
long tLastVibration = 0;
long loopStart, loopEnd = 0;
// Vibration logging
float acc_xyz[IMU_N_SAMPLES][3], acc_mean_xyz[3], acc_std_xyz[3];
// Shot timer animation state
bool timerActive = false;
long timerStart = 0;
long tLastTimerFrame = 0;
// Time the timer UI started to appear, and the time the counter started
// to fade in (-1 while the robot eyes are still closing)
long timerShownAt = 0;
long textShownAt = -1;
// Last drawn timer values, used to animate the timer out
float lastTimerProgress = 0;
uint8_t lastTimerSeconds = 0;


/**
 * Draws one frame of the running shot timer: the ring grows continuously
 * with the elapsed time, the counter shows the full seconds. Frames are
 * limited to one every FRAME_TIME_MS unless force is set.
 *
 * Intro animation: the ring's track sweeps in from its start, the counter is
 * written by the pen dot as soon as the robot eyes have merged.
 */
void timerFrame(bool force = false) {
  if(!timerActive) return;

  long now = millis();
  if(!force && (now - tLastTimerFrame) < FRAME_TIME_MS) return;
  tLastTimerFrame = now;

  // The displayed time may only run ahead of the last detected vibration by
  // two vibration checks (plus a small margin), so the display keeps moving
  // smoothly during the shot (even if a single check misses the vibration)
  // but stops right where the vibration ended. The grace period that
  // confirms the end of the shot is not counted.
  long tDisplay = now;
  long tDisplayMax = tLastVibration + 2*IMU_N_SAMPLES*IMU_READ_DELAY_MS + 20;
  if(tDisplay > tDisplayMax) tDisplay = tDisplayMax;

  float timerSeconds = ((float)(tDisplay - timerStart)) / TIMER_INCREMENT_MS;
  float timerProgress = (timerSeconds / TIMER_RING_MAX_SEC) * 100.0;
  if(timerProgress > 100.0) timerProgress = 100.0;

  lastTimerProgress = timerProgress;
  lastTimerSeconds = timerSeconds;

  // Ring build-up: first the gray track runs around the ring with an even
  // speed, then the colored fill (with its glowing head) grows from the
  // start to the current shot time
  float tBuild = (float)(now - timerShownAt) / TRANSITION_RING_BUILD_MS;
  ring.setTrack(100.0f * Easing::inOutCubic(tBuild));
  if(tBuild >= 1.0f) {
    float tFill = (float)(now - timerShownAt - TRANSITION_RING_BUILD_MS) / TRANSITION_RING_FILL_MS;
    ring.setFillVisible(true);
    ring.updatePrec(timerProgress * Easing::outCubic(tFill), 0);
  }

  // Counter and status share the area with the eyes, wait until they are gone
  if(!greeting.isVisible()) {
    if(textShownAt < 0) {
      // The dot the robot eyes merged into becomes the pen that writes
      // the first number
      textShownAt = now;
      float penX, penY;
      greeting.mergeDotPosition(penX, penY);
      counter.startWriting(penX, penY);
      counter.update(lastTimerSeconds, COUNTER_COLOR);
    }
    float tText = (float)(now - textShownAt);
    uint8_t textAlpha = Easing::alpha(tText / TRANSITION_TEXT_FADE_MS);
    status.showStatus(StatusIndicatorState::TIMER, Graphics::blend(STATUS_COLOR, textAlpha));
    // The number being written stays fixed until the pen is done, then the
    // counter catches up with the running time
    counter.setWriting(tText / TRANSITION_DIGIT_DRAW_MS);
    if(!counter.isWriting()) counter.update(lastTimerSeconds, COUNTER_COLOR);
  }
}

/**
 * Advances all running animations by one frame if one is due.
 */
void animateFrame() {
  PERF_BEGIN();
  greeting.animateEyes();
  timerFrame();
  PERF_END();
}


/**
 * Waits for the given number of milliseconds while keeping the animations
 * (robot eyes, shot timer) alive. Sampling the IMU blocks the main loop for a
 * considerable amount of time, so without pumping the animations here the
 * display would only ever update at the sampling rate instead of the intended
 * frame rate.
 */
void waitWhileAnimating(uint32_t ms) {
    uint32_t tStart = millis();
    while(millis() - tStart < ms) {
        animateFrame();
        sleep_ms(1);
    }
}

/**
 * Waits until the next frame of an animation loop is due, keeping the other
 * animations alive meanwhile. Frames are paced to a fixed FRAME_TIME_MS grid
 * (the time spent drawing counts into the frame), so the loop runs at the full
 * frame rate instead of drawing time + FRAME_TIME_MS.
 */
void waitForNextFrame(uint32_t& nextFrameAt) {
    uint32_t now = millis();
    nextFrameAt += FRAME_TIME_MS;
    // Fell behind (e.g. a slow frame): continue from now instead of rushing
    if((int32_t)(now - nextFrameAt) > FRAME_TIME_MS) nextFrameAt = now;
    while((int32_t)(millis() - nextFrameAt) < 0) {
        animateFrame();
        sleep_ms(1);
    }
}

/**
 * Checks for vibrations by reading samples from the accelerometer, 
 * calculating the standard deviation of these samples and comparing
 * to a threshold. 
 */
bool checkVibration() {
  // Read Sensor IMU_N_SAMPLES times
  for(int i=0; i<IMU_N_SAMPLES; i++) {
    // Get current XYZ accelerations
    QMI8658_read_acc_xyz(acc_xyz[i]);
#ifdef PERF_BENCH
    // Benchmark: a fake shot of 14 s every 30 s, starting 5 s after boot.
    // The sensor is still read above, so the timing stays realistic.
    static uint32_t lcg = 12345;
    bool fakeVibration = ((millis() + 25000) % 30000) < 14000 && millis() > 4000;
    for(int k = 0; k < 3; k++) {
      lcg = lcg * 1664525u + 1013904223u;
      acc_xyz[i][k] = fakeVibration ? (float)((lcg >> 16) % 200) / 100.0f : 0.0f;
    }
#endif
    // Wait for a short time before reading next sample
    waitWhileAnimating(IMU_READ_DELAY_MS);
  }

  PERF_TAG("vibcheck-samples-done");
  // Calculate mean for each axis
  for(int i=0; i<3; i++) {
    acc_mean_xyz[i] = 0;
    for(int j=0; j<IMU_N_SAMPLES; j++) {
      acc_mean_xyz[i] += acc_xyz[j][i];
    }
    acc_mean_xyz[i] /= IMU_N_SAMPLES;
  }

  // Calculate std for each axis
  for(int i=0; i<3; i++) {
    acc_std_xyz[i] = 0;
    for(int j=0; j<IMU_N_SAMPLES; j++) {
      acc_std_xyz[i] += pow(acc_xyz[j][i] - acc_mean_xyz[i], 2);
    }
    acc_std_xyz[i] = sqrtf(acc_std_xyz[i] / IMU_N_SAMPLES);

    // Check if threshold is exceeded
    if(acc_std_xyz[i] > WAKE_THRESH) {
      return true;
    }
  }

  return false;
}


void timerOutro();

/**
 * Routine that shows the shot timer while a continuous vibration is detected.
 * The ring and the counter are animated at FRAME_TIME_MS (~60 FPS) while the
 * IMU is sampled. Finishes once no vibration was detected for one timer second.
 */
void timerRoutine(long tStart) {
  // The robot eyes merge into a dot in the center, the counter appears
  // from there. The ring sweeps in meanwhile.
  greeting.merge(TRANSITION_EYES_MERGE_MS);
  
  // Setup ring for timer mode - leave gap at bottom for "s" indicator
  ring.setLimits(0.2*PI, 1.8*PI);
  // No offset needed - keeps gap at bottom naturally
  ring.setOffset(0);
  // Track, gradient, glowing head and target window
  ring.setTimerStyle(true);
  ring.setHead(true);
  ring.setFillVisible(false);
  
  // Show timer UI, the intro animation runs within timerFrame()
  timerShownAt = millis();
  textShownAt = -1;
  timerStart = tStart;
  timerActive = true;
  // The caller just confirmed a vibration
  tLastVibration = millis();
  timerFrame(true);
  
  // Keep the timer running as long as the machine vibrates. The frames are
  // drawn from within checkVibration() while the IMU is sampled.
  while(true) {
    PERF_TAG("timer-loop");
    if(checkVibration()) {
      tLastVibration = millis();
    } else if((millis() - tLastVibration) > TIMER_INCREMENT_MS) {
      break;
    }
  }

  timerActive = false;
  timerOutro();
}

/**
 * Shows the final shot time for TIMER_RESULT_HOLD_MS, then animates the timer
 * out: the counter and the status symbol fade out, the ring retracts to its
 * start and the robot eyes pop open again (if no water sensor is used).
 */
void timerOutro() {
  // Shot ended before the counter appeared: remove the dot of the eyes
  if(textShownAt < 0) {
    uint32_t nextFrameAt = millis();
    while(greeting.isVisible()) waitForNextFrame(nextFrameAt);
    greeting.clearMergeDot();
  } else {
    // Very short shot: let the pen finish writing the number first
    uint32_t nextFrameAt = millis();
    while((long)(millis() - textShownAt) < TRANSITION_DIGIT_DRAW_MS) {
      counter.setWriting((float)(millis() - textShownAt) / TRANSITION_DIGIT_DRAW_MS);
      waitForNextFrame(nextFrameAt);
    }
    counter.setWriting(1.0f);
  }

  waitWhileAnimating(TIMER_RESULT_HOLD_MS);

  // The outro runs in calm steps, one after another, so every frame has
  // only a little to draw:
  //  1. counter and status symbol fade out
  //  2. the colored fill retracts to the start of the ring
  //  3. the gray track runs back to the start, halfway through the robot
  //     eyes pop open again
  const long fillStartMs = TRANSITION_TEXT_FADE_MS;
  const long trackStartMs = fillStartMs + TRANSITION_RING_FILL_MS;

  long tStart = millis();
  uint32_t nextFrameAt = tStart;
  bool textGone = false;
  bool fillGone = false;
  bool eyesOpened = false;

  while(true) {
    PERF_BEGIN();
    long elapsed = millis() - tStart;

    // Fade out counter and status symbol
    if(!textGone) {
      float fade = 1.0f - (float)elapsed / TRANSITION_TEXT_FADE_MS;
      if(fade <= 0) {
        counter.hide();
        status.hide();
        textGone = true;
      } else {
        uint8_t textAlpha = Easing::alpha(fade);
        status.showStatus(StatusIndicatorState::TIMER, Graphics::blend(STATUS_COLOR, textAlpha));
        counter.update(lastTimerSeconds, Graphics::blend(COUNTER_COLOR, textAlpha));
      }
    }

    // Retract the colored fill to the start, then remove its last dot
    if(!fillGone && elapsed >= fillStartMs) {
      float t = (float)(elapsed - fillStartMs) / TRANSITION_RING_FILL_MS;
      if(t >= 1.0f) {
        ring.updatePrec(0, 0);
        ring.setFillVisible(false);
        fillGone = true;
      } else {
        ring.updatePrec(lastTimerProgress * (1.0f - Easing::inOutCubic(t)), 0);
      }
    }

    // Let the gray track run back to the start
    if(fillGone && elapsed >= trackStartMs) {
      float t = (float)(elapsed - trackStartMs) / TRANSITION_RING_BUILD_MS;

      // Open the eyes halfway through
      if(t >= 0.5f && !eyesOpened && !ENABLE_WATER_SENSOR) {
        greeting.open(TRANSITION_EYES_OPEN_MS);
        eyesOpened = true;
      }

      if(t >= 1.0f) {
        ring.hide();
        if(textGone) break;
      } else {
        ring.setTrack(100.0f * (1.0f - Easing::inOutCubic(t)));
      }
    }
    PERF_END();

    waitForNextFrame(nextFrameAt);
  }

  // Eyes not opened yet (e.g. very short outro settings)
  if(!eyesOpened && !ENABLE_WATER_SENSOR) {
    greeting.open(TRANSITION_EYES_OPEN_MS);
  }

  // Back to the plain ring (water level)
  ring.setFillVisible(true);
  ring.setHead(false);
  ring.setTimerStyle(false);
}

/**
 * Routine that tries to read a valid filllevel. If a valid value
 * is read, the filllevel is displayed after an animation.
 * If water sensor is disabled, show greeting message instead.
 */
void levelRoutine(float transitionTime) {
  // Only try to read sensor if it's enabled
  if(ENABLE_WATER_SENSOR) {
    // Reset ring limits for water level display (make space for status at bottom)
    ring.setLimits(0.2*PI, 1.8*PI);
    // Reset offset to 0 for water level display (no rotation)
    ring.setOffset(0);
    
    // Try to get fill level
    targetPerc = LevelSensor::getFillPercentage();
    
    // Choose ring color based on percentage
    if(targetPerc > LEVEL_BAD_PERC) {
      currRingColor = RING_COLOR_OK;
    } else {
      currRingColor = RING_COLOR_BAD;
    }

    if(targetPerc > -1) {
      // Valid sensor reading
      status.showStatus(StatusIndicatorState::LEVEL, STATUS_COLOR);
      counter.update(currPerc, COUNTER_COLOR);
      ring.updatePrec(currPerc, currRingColor);
      greeting.hide();
    } else {
      // Sensor error - show error state
      status.showStatus(StatusIndicatorState::ERROR, STATUS_COLOR);
      counter.hide();
      ring.updatePrec(100, currRingColor);
      greeting.hide();
    }

    // Gradually wakeup display
    for(int i=0; i < 100; i++) {
      Display::setBrightness(i);
      sleep_ms(DIM_STEP_DELAY_MS);
    }

    if(targetPerc > -1) {
      // Transition
      interpolator.setFromTo(currPerc, targetPerc, transitionTime);
      while(!interpolator.next(currPerc)) {
          loopStart = millis();
          ring.updatePrec(currPerc, currRingColor);
          counter.update(currPerc, COUNTER_COLOR);
          loopEnd = millis();
          if(loopEnd-loopStart < FRAME_TIME_MS) {
              sleep_ms(FRAME_TIME_MS - (loopEnd-loopStart));
          }
      }
    }
  } else {
    status.hide();
    counter.hide();
    ring.hide();  // Hide ring completely when no sensor
    
    // Gradually wakeup display
    for(int i=0; i < 100; i++) {
      Display::setBrightness(i);
      sleep_ms(DIM_STEP_DELAY_MS);
    }

    // Let the robot eyes pop open
    greeting.open(TRANSITION_EYES_OPEN_MS);
  }
}

/**
 * Wakes up the system by triggering the level routine and resetting the current fill percentage.
 */
void systemWake() {
  // Reset level
  currPerc = 0;
  isAwake = true;
  levelRoutine(TRANSITION_TIME_LONG_MS);
  
}


/**
 * Puts the system to sleep state by dimming the display and hiding UI elements
 * Note: The MCU is not actually put to sleep, just the display is turned off.
 */
void systemSleep() {
  // Let the robot eyes slowly close before the display goes dark
  if(greeting.isVisible()) {
    greeting.close(TRANSITION_EYES_CLOSE_MS);
    uint32_t nextFrameAt = millis();
    while(greeting.isVisible()) {
      waitForNextFrame(nextFrameAt);
    }
  }

  // Gradually dim display
  for(int i=99; i > -1; i--) {
    Display::setBrightness(i);
    sleep_ms(DIM_STEP_DELAY_MS);
  }

  // Hide UI elements
  counter.hide();
  status.hide();
  ring.hide();
  greeting.hide();

  // Wipe display
  Display::clear(BLACK);

  isAwake = false;
}


void setup() {

    Serial.begin(9600);

    // Initialize display
    Display::init();
    Display::clear(BLACK);

    // Initialize IMU
    QMI8658_init();

    // Initialize level sensor only if enabled
    if(ENABLE_WATER_SENSOR) {
      LevelSensor::init();
      // Update ring limits to make space for status symbol at the bottom
      ring.setLimits(0.2*PI, 1.8*PI);
    }

    systemWake();

}

void loop() {
    PERF_TAG("loop");

    isVibrating = checkVibration();
    if(isVibrating && !isVibratingPrev) tLastVibration = millis();

    if(!isAwake && isVibrating) {
      // System is sleeping and vibrating
      systemWake();
    } else if(isVibrating) {
      // System is awake and vibrating
      // Wait for timer trigger delay
      waitWhileAnimating(TIMER_TRIGGER_DELAY_MS);
      // Start timer if enabled and still vibrating
      if(ENABLE_TIMER && checkVibration()) {
        timerRoutine(tLastVibration);
        // Vibration stopped, go back to show fill level.
        // Without sensor, timerRoutine already brought back the robot eyes.
        if(ENABLE_WATER_SENSOR) levelRoutine(TRANSITION_TIME_SHORT_MS);
      }
      
    } else if(isAwake && ((millis() - tLastVibration) > SLEEP_TIMEOUT_MS)) {
      // Sleep due to timeout
      systemSleep();
    }

    greeting.animateEyes();
    isVibratingPrev = isVibrating;
}