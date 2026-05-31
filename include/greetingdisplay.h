#ifndef _GREETINGDISPLAY_H
#define _GREETINGDISPLAY_H

#include <pico/stdlib.h>

/**
 * GreetingDisplay class that shows animated robotic eyes while waiting.
 */
class GreetingDisplay {
private:
    uint8_t _xCenter;
    uint8_t _yCenter;
    bool _hidden;
    uint32_t _lastAnimationMillis;
    uint32_t _lastBlinkMillis;
    uint32_t _lastGazeMillis;
    uint32_t _nextBlinkDelay;
    int8_t _currentGazeX;
    int8_t _currentGazeY;
    int8_t _prevGazeX;
    int8_t _prevGazeY;
    bool _isBlinking;
    bool _isExcited;
    bool _prevEyeClosed;
    uint32_t _blinkStartMillis;
    uint32_t _lastMouthAnimationMillis;
    uint32_t _lastMouthExpressionMillis;
    uint32_t _nextMouthExpressionDelay;
    uint8_t _mouthAnimationPhase;
    uint8_t _mouthExpression;
    uint16_t _color;

    static constexpr uint8_t EYE_POD_WIDTH = 64;
    static constexpr uint8_t EYE_POD_HEIGHT = 38;
    static constexpr uint8_t EYE_GAP = 24;
    static constexpr uint8_t EYE_PUPIL_RADIUS = 8;
    static constexpr uint8_t EYE_CORNER_RADIUS = 8;
    static constexpr uint8_t ANIMATION_INTERVAL_MS = 35;
    static constexpr uint16_t FRAME_HOLD_MS = 1200;
    static constexpr uint16_t BLINK_DURATION_MS = 280;
    static constexpr uint16_t BLINK_INTERVAL_MIN_MS = 3800;
    static constexpr uint16_t BLINK_INTERVAL_MAX_MS = 6200;
    static constexpr uint8_t MAX_GAZE_OFFSET_X = 7;
    static constexpr uint8_t MAX_GAZE_OFFSET_Y = 4;
    static constexpr uint8_t EYE_AREA_PADDING = 10;
    static constexpr uint8_t MOUTH_OFFSET_Y = 30;
    static constexpr uint8_t MOUTH_HALF_WIDTH = 18;
    static constexpr uint16_t MOUTH_ANIMATION_INTERVAL_MS = 260;
    static constexpr uint16_t MOUTH_EXPRESSION_INTERVAL_MIN_MS = 2400;
    static constexpr uint16_t MOUTH_EXPRESSION_INTERVAL_MAX_MS = 5200;
    static constexpr uint8_t MOUTH_ANIMATION_STEP_COUNT = 6;
    static constexpr uint8_t MOUTH_EXPRESSION_SMILE = 0;
    static constexpr uint8_t MOUTH_EXPRESSION_BOW = 1;
    static constexpr uint8_t MOUTH_EXPRESSION_DOT = 2;

    void clearEyeArea();
    void eraseEyeInterior(uint8_t xCenter, uint8_t yCenter);
    void erasePreviousPupil(uint8_t xCenter, uint8_t yCenter, int8_t prevOffsetX, int8_t prevOffsetY);
    void drawEyePod(uint8_t xCenter, uint8_t yCenter, uint16_t color);
    void drawPupil(uint8_t xCenter, uint8_t yCenter, int8_t offsetX, int8_t offsetY, uint16_t color);
    void drawBlink(uint8_t xCenter, uint8_t yCenter, uint16_t color);
    void drawMouth(uint8_t xCenter, uint8_t yCenter, uint16_t color);
    void chooseNewGaze();
    void chooseNewMouthExpression();
    void stepAnimation();

public:
    GreetingDisplay();

    void showRandom(uint16_t color);
    void hide();
    void animateEyes();
};

#endif

