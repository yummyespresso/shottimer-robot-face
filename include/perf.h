#ifndef _PERF_H
#define _PERF_H

/**
 * Performance measurement for the animations (only active when the firmware
 * is built with -DPERF_BENCH, see the [env:bench] section in platformio.ini).
 *
 * Every rendered frame is timed. Every PERF_WINDOW_MS a line is printed over
 * USB serial with the number of frames, the average and maximum render time
 * and the largest gap between two frames in that window.
 */

#ifdef PERF_BENCH

#include <Arduino.h>

#define PERF_WINDOW_MS 250
// Calls shorter than this did not draw anything (frame not due yet)
#define PERF_MIN_FRAME_US 150

namespace Perf {
    inline uint32_t windowStart = 0;
    inline uint32_t frames = 0;
    inline uint32_t sumUs = 0;
    inline uint32_t maxUs = 0;
    inline uint32_t maxGapUs = 0;
    inline uint32_t lastFrameStart = 0;
    inline uint32_t currentStart = 0;
    // Name of the last code location passed (see PERF_TAG)
    inline const char* lastTag = "";

    inline void begin() {
        currentStart = micros();
    }

    inline void end() {
        uint32_t now = micros();
        uint32_t duration = now - currentStart;
        if(duration >= PERF_MIN_FRAME_US) {
            frames++;
            sumUs += duration;
            if(duration > maxUs) maxUs = duration;
            if(lastFrameStart != 0) {
                uint32_t gap = currentStart - lastFrameStart;
                if(gap > maxGapUs) maxGapUs = gap;
                if(gap > 30000 && gap < 1000000) {
                    Serial.printf("GAP t=%lu gap=%lu last=%s\n", (unsigned long)millis(),
                        (unsigned long)gap, lastTag);
                }
            }
            lastFrameStart = currentStart;
        }
        uint32_t ms = millis();
        if(ms - windowStart >= PERF_WINDOW_MS) {
            if(frames > 0) {
                lastTag = "perf-print";
                Serial.printf("PERF t=%lu frames=%lu avg=%lu max=%lu maxgap=%lu\n",
                    (unsigned long)ms, (unsigned long)frames, (unsigned long)(sumUs / frames),
                    (unsigned long)maxUs, (unsigned long)maxGapUs);
            }
            windowStart = ms;
            frames = 0;
            sumUs = 0;
            maxUs = 0;
            maxGapUs = 0;
        }
    }
}

#define PERF_BEGIN() Perf::begin()
#define PERF_END() Perf::end()
#define PERF_TAG(name) (Perf::lastTag = (name))

#else

#define PERF_TAG(name)

#define PERF_BEGIN()
#define PERF_END()

#endif

#endif
