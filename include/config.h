#ifndef _CONFIG_H
#define _CONFIG_H

// *******************************
//    Waterlevel sensor settings
// *******************************
// Set to true if you have a water level sensor installed, false if not
// When false, the system will display coffee-related greeting messages instead
#define ENABLE_WATER_SENSOR false
// Profitec Go water reservoir is ~25cm deep
// The intake for the pump sticks out about 2cm
// The sensor sticks around ~2cm into the tank
// So at 25cm - 2cm - 2cm = 21cm = 210mm, the pump starts sucking air (0% fill level)
#define LEVEL_MAX_DIST_MM 210
// DF-A02YYUW sensor has a blind distance of 3cm = 30mm
#define LEVEL_MIN_DIST_MM 30
// Number of times to try reading a value from the sensor
#define LEVEL_N_RETRY 10
// Delay between two consecutive read attempts
#define LEVEL_READ_DELAY_MS 10
// Percentage value below which the ring changes color from OK to BAD
#define LEVEL_BAD_PERC 25

// *******************************
//       IMU sensor settings
// *******************************
// Number of samples to read from IMU
#define IMU_N_SAMPLES 10
// Delay between two consecutive reads
#define IMU_READ_DELAY_MS 10
// Minimum required standard deviation among samples to detect a vibration
#define WAKE_THRESH 0.5

// *******************************
//         Timer settings
// *******************************
// If you don't need the timer, you can disable it.
#define ENABLE_TIMER true
// How much milliseconds one increment of the counter needs
// For a precise shot time measurement, 1000ms should be used for a full second
// My Profitec GO seems to count a bit faster than 1000ms
#define TIMER_INCREMENT_MS 975
// The timer is started when the system is awake and a vibration is detected
// The trigger delay defines how many milliseconds the system has to vibrate
// while being awake until the timer is activated. This prevents the timer
// from starting when e.g. the portafilter is removed/inserted
#define TIMER_TRIGGER_DELAY_MS 2000
// Target window of the shot time in seconds. The window is highlighted on the
// ring, the ring turns green inside it and orange/red after it
#define TARGET_WINDOW_START_SEC 25
#define TARGET_WINDOW_END_SEC 30
// Shot time in seconds at which the ring is completely filled
#define TIMER_RING_MAX_SEC 35


// *******************************
//         Counter settings
// *******************************
// Center point position of counter in pixels
#define COUNTER_CENTER_X 120
#define COUNTER_CENTER_Y 120
// Color of counter
#define COUNTER_COLOR WHITE

// *******************************
//     RingIndicator settings
// *******************************
// Center point position of ring-indicator in pixels
#define RING_CENTER_X 120
#define RING_CENTER_Y 120
// Radius of ring-indicator in pixels
#define RING_RADIUS 100
// Width of ring-indicator in pixels
#define RING_WIDTH 15
// Color of ring-indicator if fill level percentage > LEVEL_BAD_PERC
#define RING_COLOR_OK CYAN
// Color of ring-indicator if fill level percentage <= LEVEL_BAD_PERC
#define RING_COLOR_BAD BRRED
// Shot timer ring colors. The ring shows a gradient along these colors:
// Color at the start of the shot (same as the robot eyes)
#define RING_COLOR_TIMER_START FACE_EYE_COLOR
// Color shortly before the target window
#define RING_COLOR_TIMER BLUE
// Color inside the target window
#define RING_COLOR_TIMER_COMPLETE 0x0560
// Color right after the target window (between green and orange)
#define RING_COLOR_TIMER_WARN 0xE6A0
// Color shortly after the target window
#define RING_COLOR_TIMER_OVER 0xFC00
// Color at the end of the ring (TIMER_RING_MAX_SEC)
#define RING_COLOR_TIMER_LATE RED
// Track along the whole ring that shows how far the shot can go:
// width in pixels, color and brightness (0-255)
#define RING_TRACK_WIDTH RING_WIDTH
#define RING_TRACK_COLOR WHITE
#define RING_TRACK_ALPHA 60
// Color of the track inside the target window (pale, dark green)
#define RING_TRACK_WINDOW_COLOR 0x2A46

// *******************************
//    StatusIndicator settings
// *******************************
// Center point position of status-indicator in pixels
#define STATUS_CENTER_X 120
#define STATUS_CENTER_Y 215
// Color of status-indicator
#define STATUS_COLOR WHITE

// *******************************
//       Animation settings
// *******************************
// Target frame time for ring and timer animation in milliseconds (16 ms = ~60 FPS)
#define FRAME_TIME_MS 16
// Animation time for ring-indicator when system wakes up
#define TRANSITION_TIME_LONG_MS 3000
// Animation time for ring-indicator after timer ends
#define TRANSITION_TIME_SHORT_MS 1500
// Robot eyes <-> shot timer transition:
// Time the robot eyes need to squeeze into lines, slide together and merge
// into a glowing dot when a shot starts (the counter fades in afterwards)
#define TRANSITION_EYES_MERGE_MS 1000
// Time the robot eyes need to close before the display goes to sleep
#define TRANSITION_EYES_CLOSE_MS 440
// Time the robot eyes need to fade in and pop open
#define TRANSITION_EYES_OPEN_MS 450
// Time the gray track of the timer ring needs to run around the ring
// (shot start) or back to its start (shot end)
#define TRANSITION_RING_BUILD_MS 700
// Time the colored fill needs to grow to the current shot time (shot start)
// or to retract to the start of the ring (shot end)
#define TRANSITION_RING_FILL_MS 350
// Fade time of the counter and the status symbol
#define TRANSITION_TEXT_FADE_MS 200
// Time the dot (left over from the robot eyes) needs to move to the start of
// the first number, write it like a pen and fade out
#define TRANSITION_DIGIT_DRAW_MS 1200
// Radius of that pen dot in pixels (its color is FACE_EYE_COLOR)
#define PEN_DOT_RADIUS 5.0f
// How long the final shot time stays on screen before switching back
#define TIMER_RESULT_HOLD_MS 2000

// *******************************
//        Display settings
// *******************************
// Rotation of display
// Depending on the side at which the sensor is mounted, a different rotation
// value is required:
// Right:       90
// Left:        270
// Top:         0
// Bottom:      180
#define ROTATION 270
// Time after which the display goes dark when no vibration was detected
#define SLEEP_TIMEOUT_MS 60000
// Delay between dimming steps. Dimming is performed from 100 to 0 in decrements of 1.
#define DIM_STEP_DELAY_MS 1


// *******************************
//       Robot face settings
// *******************************
// Color of the robot eyes (RGB565).
#define FACE_EYE_COLOR 0x073E
// Color of the bright top edge of the eyes (RGB565).
#define FACE_EYE_TOP_COLOR 0x07FF
// Color of the dark bottom edge of the eyes (RGB565).
#define FACE_EYE_BOTTOM_COLOR 0x04B0


#endif