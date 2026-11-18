#ifndef _EASING_H
#define _EASING_H

/**
 * Easing helpers for animations. All functions map t in [0, 1] to [0, 1].
 */
namespace Easing {
    inline float clamp01(float t) {
        return t < 0 ? 0 : (t > 1 ? 1 : t);
    }

    // Fast start, gentle stop
    inline float outCubic(float t) {
        t = clamp01(t);
        float u = 1.0f - t;
        return 1.0f - u * u * u;
    }

    // Gentle start and stop
    inline float inOutCubic(float t) {
        t = clamp01(t);
        if(t < 0.5f) return 4.0f * t * t * t;
        float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u / 2.0f;
    }

    // Smooth start and stop, softer than inOutCubic
    inline float smooth(float t) {
        t = clamp01(t);
        return t * t * (3.0f - 2.0f * t);
    }

    // Converts a fade value in [0, 1] to an alpha value in [0, 255]
    inline uint8_t alpha(float t) {
        return (uint8_t)(clamp01(t) * 255.0f + 0.5f);
    }
}

#endif
