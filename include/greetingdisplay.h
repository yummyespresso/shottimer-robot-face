#ifndef _GREETINGDISPLAY_H
#define _GREETINGDISPLAY_H

#include <pico/stdlib.h>

/**
 * GreetingDisplay class that shows a pair of glowing robot eyes while waiting.
 * The eyes are rounded squares ("squircles") filled with a bright accent color.
 * Emotions are expressed purely by morphing the shape of the eyes: squinting,
 * tilting, winking and closing flatten or shear the rounded square. Every change
 * is interpolated over several frames so the eyes glide instead of popping.
 */
class GreetingDisplay {
private:
    uint8_t _xCenter;
    uint8_t _yCenter;
    bool _hidden;
    uint32_t _lastAnimationMillis;
    uint32_t _lastBlinkMillis;
    uint32_t _nextBlinkDelay;
    bool _isBlinking;
    uint32_t _blinkStartMillis;
    uint32_t _lastExpressionMillis;
    uint32_t _nextExpressionDelay;
    uint8_t _expression;
    // Interpolated shape of both eyes. `Start` is the pose the current
    // transition began from, `Target` the pose it is heading to.
    struct Pose {
        int8_t height;
        int8_t skew;
    };
    Pose _leftCurrent;
    Pose _rightCurrent;
    Pose _leftStart;
    Pose _rightStart;
    Pose _leftTarget;
    Pose _rightTarget;
    uint8_t _transitionFrame;
    uint8_t _transitionFrames;

    // Opening / closing animation (used when switching to and from the timer)
    static constexpr uint8_t MODE_NORMAL = 0;
    static constexpr uint8_t MODE_OPENING = 1;
    static constexpr uint8_t MODE_CLOSING = 2;
    static constexpr uint8_t MODE_MERGING = 3;
    uint8_t _mode;
    // Area covered by the last frame of the merge animation (capsule phase)
    int _mergeX0, _mergeY0, _mergeX1, _mergeY1;
    bool _mergeCapsules;

    // Merge animation: share of the duration for each phase
    static constexpr float MERGE_SQUINT_SHARE = 0.30f;   // eyes squeeze into lines
    static constexpr float MERGE_SLIDE_SHARE = 0.50f;    // lines slide together
    // the rest: the remaining dot glows up and fades out
    static constexpr int8_t MERGE_LINE_HEIGHT = 10;       // height scale of the lines
    static constexpr float MERGE_LINE_RADIUS = 3.0f;      // half thickness of the lines
    static constexpr float MERGE_MEET_HALF_LENGTH = 7.0f; // half length of the lines when they meet

    uint32_t _modeStartMillis;
    uint32_t _modeDuration;
    Pose _modeLeftFrom;
    Pose _modeRightFrom;
    // Brightness the eyes are currently drawn with (0 = invisible, 255 = full)
    uint8_t _alpha;

    // Eye geometry (relative to the face center)
    static constexpr int8_t EYE_LEFT_OFFSET = -46;
    static constexpr int8_t EYE_RIGHT_OFFSET = 45;
    static constexpr int8_t EYE_Y_OFFSET = 0;
    static constexpr uint8_t EYE_HALF_WIDTH = 31;
    static constexpr uint8_t EYE_HALF_HEIGHT = 27;
    static constexpr uint8_t EYE_CORNER_RADIUS = 17;
    static constexpr int8_t EYE_MAX_SKEW = 8;
    static constexpr int8_t EYE_BLINK_HEIGHT = 8;

    // Animation timing
    static constexpr uint8_t ANIMATION_INTERVAL_MS = 16;
    // Length of a shape transition in frames (~16 ms per frame).
    static constexpr uint8_t BLINK_TRANSITION_FRAMES = 6;
    static constexpr uint8_t EXPRESSION_TRANSITION_FRAMES = 7;
    static constexpr uint16_t BLINK_MIN_DURATION_MS = 60;
    static constexpr uint16_t BLINK_INTERVAL_MIN_MS = 1800;
    static constexpr uint16_t BLINK_INTERVAL_MAX_MS = 3200;
    static constexpr uint16_t EXPRESSION_INTERVAL_MIN_MS = 1000;
    static constexpr uint16_t EXPRESSION_INTERVAL_MAX_MS = 2200;
    static constexpr uint8_t FACE_AREA_PADDING = 3;

    // Expressions
    static constexpr uint8_t EXPRESSION_NEUTRAL = 0;
    static constexpr uint8_t EXPRESSION_SQUINT = 1;
    static constexpr uint8_t EXPRESSION_WINK = 2;
    static constexpr uint8_t EXPRESSION_TILT = 3;
    static constexpr uint8_t EXPRESSION_SLEEP = 4;

    void clearFaceArea();
    static int eyeHalfHeight(int heightScale);
    static int eyeExtent(int heightScale, int dy);
    static uint16_t eyeRowColor(int heightScale, int dy);
    // Anti-aliasing: the shape is sampled at AA_SUBSAMPLES x AA_SUBSAMPLES
    // sub-pixel positions per pixel, the eye color is blended with the black
    // background accordingly.
    static constexpr uint8_t AA_SUBSAMPLES = 3;
    static constexpr int AA_SCALE = 18;          // fixed point: 1/AA_SCALE pixel
    static constexpr int AA_STEP = AA_SCALE / AA_SUBSAMPLES;
    static constexpr int AA_OFFSET = AA_STEP / 2;
    // Precomputes the per-sub-sample horizontal half extents and skew offsets of
    // one eye row (values relative to the eye center, in 1/AA_SCALE pixel units).
    static void eyeRowParams(int heightScale, int skew, int dy, int* extFp, int* offFp);
    // Fraction of the pixel covered by the eye, in 1/(AA_SUBSAMPLES^2) units.
    static int eyeCoverageFast(int x, int xCenter, const int* extFp, const int* offFp);
    // Largest horizontal half extent of the eye at row `dy` (relative to the eye
    // center), taken over the row's sub-sample positions, in 1/AA_SCALE units.
    // Returns -1 if the row lies completely outside the eye.
    static int eyeExtentFp(int heightScale, int dy);
    void drawEyeShape(int xCenter, int yCenter, int heightScale, int skew);
    void drawEyeDelta(int xCenter, int yCenter,
                      int oldHeight, int oldSkew, int newHeight, int newSkew,
                      uint8_t alpha = 255);
    void resetIdleAnimation();
    void stepModeAnimation(uint32_t now);
    void stepMergeAnimation(uint32_t now);
    void drawCapsules(float xLeft, float xRight, float y, float halfLength,
                      float radius, uint8_t alpha);
    void chooseNewExpression();
    void stepAnimation();

public:
    GreetingDisplay();

    void showRandom(uint16_t color);
    void hide();
    void animateEyes();

    /**
     * Lets the eyes appear: they fade in as a thin line and open up with a
     * small overshoot. The animation advances with animateEyes().
     */
    void open(uint32_t durationMs);

    /**
     * Closes the eyes into a thin line that fades out. Once finished, the
     * eyes are hidden. The animation advances with animateEyes().
     */
    void close(uint32_t durationMs);

    /**
     * The eyes squeeze into two glowing lines that slide together in the
     * center and merge into a dot (PEN_DOT_RADIUS). Used when the shot timer
     * appears: the dot then writes the first number. Once finished, the eyes
     * count as hidden, but the dot is left on the display.
     */
    void merge(uint32_t durationMs);

    /**
     * Position of the dot the merge animation ends with. The dot stays on the
     * display after merge() finished, so another element (the counter's pen)
     * can take it over seamlessly.
     */
    void mergeDotPosition(float& x, float& y) const;

    /**
     * Removes the dot left over by merge() if nothing took it over.
     */
    void clearMergeDot();

    /**
     * True while any part of the eyes is drawn on the display
     * (also during the opening and closing animation).
     */
    bool isVisible() const { return !_hidden; }
};

#endif
