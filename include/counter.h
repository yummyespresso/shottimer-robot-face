#ifndef _COUNTER_H
#define _COUNTER_H

#include <pico/stdlib.h>

/**
 *
 * Counter class to display a number centered at a given position.
 * The digits are drawn with an anti-aliased font. Changing digits are
 * overwritten in place (no black frame in between), so updates don't flicker.
 *
 * The counter can also "write" its digits: a glowing pen dot moves to the
 * start of the first digit and draws every digit along its strokes.
 *
 */
class Counter {
private:

    // Maximum number of digits (values up to 255)
    static constexpr uint8_t MAX_DIGITS = 3;

    // Digits currently shown, most significant digit first
    uint8_t _digits[MAX_DIGITS] {0};

    // Current number of digits displayed
    uint8_t _nDigits;

    // Current color
    uint16_t _color;

    // Counter position
    int _xCenter;
    int _yCenter;

    // Digit layout: every digit occupies a cell of _cellWidth pixels,
    // the rows _top.._bottom contain all digits.
    int _cellWidth;
    int _baseline;
    int _top;
    int _bottom;

    // Writing animation
    bool _writing;
    float _writeProgress;
    // Where the pen comes from (e.g. the dot of the robot eyes)
    float _penStartX, _penStartY;
    // Current pen state, computed by updatePen()
    float _penX, _penY, _penRadius;
    uint8_t _penAlpha;
    // Reveal progress (0..1) of every digit
    float _reveal[MAX_DIGITS];
    // Extra pixels around the digits that are redrawn while the pen is shown
    int _penMargin;

    /**
     * Writes the digits of value (most significant first) into buffer.
     * Returns the number of digits.
     */
    uint8_t getDigits(uint8_t value, uint8_t* buffer);

    /**
     * Returns the x coordinate of the left edge of the first cell when
     * nDigits digits are shown centered at _xCenter.
     */
    int layoutLeft(uint8_t nDigits);

    /**
     * Pen tip position on digit index at reveal progress r.
     */
    void penOnDigit(uint8_t index, float r, float& x, float& y);

    /**
     * Computes the reveal of every digit and the pen state from _writeProgress.
     */
    void updatePen();

    /**
     * Redraws the columns x0..x1 and rows y0..y1 of the counter from the
     * current state (digits plus pen).
     */
    void render(int x0, int x1, int y0, int y1);

    /**
     * Redraws the columns x0..x1 of all digit rows.
     */
    void render(int x0, int x1);

public:
    Counter();

    /**
     * Updates the counter with the new given value and color.
     * Only the parts that changed are redrawn.
     */
    void update(uint8_t value, uint16_t color);

    /**
     * Hides the counter and resets _nDigits to zero.
     * Also stops a running writing animation.
     */
    void hide();

    /**
     * Starts the writing animation. The pen dot starts at (penX, penY), moves
     * to the start of the first digit and writes the digits one after another.
     * Nothing of the digits is visible until the pen draws it.
     */
    void startWriting(float penX, float penY);

    /**
     * Advances the writing animation to progress t (0..1) and redraws the
     * counter. At t = 1 the pen has faded out and the digits are complete.
     */
    void setWriting(float t);

    /**
     * True while the writing animation runs.
     */
    bool isWriting() const { return _writing; }

};


#endif
