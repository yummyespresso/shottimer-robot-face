#ifndef _RINGINDICATOR_H
#define _RINGINDICATOR_H

#include <pico/stdlib.h>

/**
 *
 * RingIndicator class manages the state of the ring indicator including drawing it.
 *
 * The ring is an arc with rounded ends, drawn with anti-aliased edges.
 * Every pixel's coverage is computed analytically from its distance to the
 * arc's center line, so the edges are smooth at any angle.
 * Updates only redraw the part of the ring that actually changed (the area
 * around the moving end), which keeps a frame cheap enough for 60 FPS.
 *
 * Two styles are available:
 *  - plain (water level): one arc in one color
 *  - timer: a dim track along the whole ring with a highlighted target
 *    window, a fill with a color gradient along the ring (cool -> green in
 *    the target window -> orange/red when too long) and a glowing head at
 *    the end of the fill.
 *
 */
class RingIndicator {

private:
    // Number of entries of the color gradient look-up table
    static constexpr int GRADIENT_SIZE = 256;

    // If indicator is currently visible or not
    bool _hidden;

    // Current color of indicator (plain style)
    uint16_t _color;

    // Center position, radius and width of arc
    uint8_t _xCenter, _yCenter, _radius, _width;

    // radValue:        Current value in radiants
    // radOffset:       Shift value in radiants to shift complete arc
    // radMin:          Minimum value of arc start
    // radMax:          Maximum value of arc end
    float _radValue, _radOffset, _radMin, _radMax;

    // Timer style state
    bool _timerStyle;
    float _trackValue;          // end of the visible track in radiants
    bool _head;                 // glowing head at the end of the fill
    bool _fillVisible;          // fill (and head) shown on top of the track
    uint16_t _gradient[GRADIENT_SIZE];

    // Cached geometry of the current state in fixed point, updated by
    // prepareGeometry(). Angles are in units of 2*PI/65536 relative to the
    // start of the arc, positions in 1/16 pixel.
    uint16_t _startU;
    int32_t _spanU, _trackSpanU, _fullSpanU;
    int32_t _windowStartU, _windowEndU;
    int32_t _capSX, _capSY, _capEX, _capEY, _capTX, _capTY;
    int32_t _gradientScale;
    uint16_t _headColor;
    uint16_t _trackColor;

    /**
     * Computes the color gradient of the timer style (once, at boot).
     */
    void buildGradient();

    /**
     * Updates the cached geometry from the current state.
     */
    void prepareGeometry();

    /**
     * Returns the gradient color at angle t (units, relative to the arc start).
     */
    uint16_t gradientAt(int32_t t);

    /**
     * Returns the color of a ring pixel. angle and radius come from the ring's
     * look-up table (see ringindicator.cpp), x / y are the pixel position.
     */
    uint16_t pixelColor(int x, int y, uint16_t angle, uint16_t radius);

    /**
     * Redraws all ring pixels between the absolute angles radFrom and radTo,
     * including the rounded ends and the head's glow that stick out at both
     * sides. Only pixels of the ring band are touched, so other UI elements
     * are never overwritten.
     */
    void renderAngles(float radFrom, float radTo);

public:
    RingIndicator();

    /**
     * Update minimum and maximum radiant values of arc.
     */
    void setLimits(float radMin, float radMax);

    /**
     * Set the rotation offset for the arc in radians.
     * This shifts where the arc starts and ends by the given offset.
     * Useful for rotating the arc to start from a different position (e.g., top instead of bottom).
     */
    void setOffset(float radOffset);

    /**
     * Switches between the plain style (false) and the timer style (true).
     * Hides the ring. The timer style uses the timer settings in config.h,
     * its colors come from the gradient (the color parameters of updatePrec
     * and update are ignored).
     */
    void setTimerStyle(bool timerStyle);

    /**
     * Timer style: sets how much of the track is visible, in percent of the
     * whole ring.
     */
    void setTrack(float percValue);

    /**
     * Timer style: shows or hides the glowing head at the end of the fill.
     */
    void setHead(bool head);

    /**
     * Timer style: shows or hides the fill (and its head), e.g. to build up
     * the track first and let the fill grow afterwards.
     */
    void setFillVisible(bool visible);

    /**
     * Display a percentage value with the ring indicator.
     * The arc is drawn from _radMin to _radValue,
     * where _radValue is:
     *      _radMin                                         if percValue <= 0
     *      _radMax                                         if percValue >= 100
     *      (percValue/100)*(_radMax - _radMin) + _radMin   otherwise
     */
    void updatePrec(float percValue, uint16_t color);

    /**
     * Display a radiant value with the ring indicator.
     * The arc is drawn from _radMin to _radValue,
     * where _radValue is:
     *      _radMin                                         if radValue <= _radMin
     *      _radMax                                         if radValue >= _radMax
     *      radValue                                        otherwise
     */
    void update(float radValue, uint16_t color);

    /**
     * Hides the ring indicator and resets its value to _radMin.
     * In timer style the track is removed as well.
     */
    void hide();

};

#endif
