#include <greetingdisplay.h>
#include <graphics.h>
#include <display.h>
#include <config.h>
#include <pico/rand.h>
#include <math.h>

static inline void drawHorizontalLine(int x0, int y, int x1, uint16_t color) {
    if(x0 > x1) return;
    for(int x = x0; x <= x1; x++) {
        Display::setPixel(x, y, color);
    }
}

static inline void fillRect(int x0, int y0, int width, int height, uint16_t color) {
    for(int y = y0; y < y0 + height; y++) {
        for(int x = x0; x < x0 + width; x++) {
            Display::setPixel(x, y, color);
        }
    }
}

// Integer square root (Newton's method), used for the rounded corners.
static inline int isqrt(int n) {
    if(n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while(y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Row buffer used to write a whole eye row in a single SPI transfer.
static uint16_t eyeRowBuffer[LCD_WIDTH];

// Blends the eye color with the black background. `coverage` is the fraction of
// the pixel covered by the eye, given in 1/AA_MAX_COVERAGE units.
static inline uint16_t blendWithBackground(uint16_t color, int coverage, int maxCoverage) {
    int r = ((color >> 11) & 0x1F) * coverage / maxCoverage;
    int g = ((color >> 5) & 0x3F) * coverage / maxCoverage;
    int b = (color & 0x1F) * coverage / maxCoverage;
    return (uint16_t)((r << 11) | (g << 5) | b);
}


/**
 * Fast coverage of the pixels of one eye row. Pixels whose sub-samples are
 * all inside (or all outside) the shape are recognised with a few integer
 * comparisons, only the anti-aliased edge pixels are sampled. The colors for
 * every coverage level are prepared once per row.
 */
struct EyeRowShader {
    int fullLo, fullHi, emptyLo, emptyHi;
    bool any, all;
    uint16_t palette[17];
};

static void eyeRowPrepare(EyeRowShader& r, const int* ext, const int* off, int n,
                          uint16_t color, int maxCoverage) {
    r.any = false;
    r.all = true;
    r.fullLo = -1000000; r.fullHi = 1000000;
    r.emptyLo = 1000000; r.emptyHi = -1000000;
    for(int i = 0; i < n; i++) {
        if(ext[i] < 0) { r.all = false; continue; }
        r.any = true;
        int lo = off[i] - ext[i], hi = off[i] + ext[i];
        if(lo > r.fullLo) r.fullLo = lo;
        if(hi < r.fullHi) r.fullHi = hi;
        if(lo < r.emptyLo) r.emptyLo = lo;
        if(hi > r.emptyHi) r.emptyHi = hi;
    }
    for(int c = 0; c <= maxCoverage && c < 17; c++) {
        r.palette[c] = (c == 0) ? BLACK : blendWithBackground(color, c, maxCoverage);
    }
}

GreetingDisplay::GreetingDisplay() :
    _xCenter(COUNTER_CENTER_X),
    _yCenter(COUNTER_CENTER_Y),
    _hidden(true),
    _lastAnimationMillis(0),
    _lastBlinkMillis(0),
    _nextBlinkDelay(0),
    _isBlinking(false),
    _blinkStartMillis(0),
    _lastExpressionMillis(0),
    _nextExpressionDelay(0),
    _expression(EXPRESSION_NEUTRAL),
    _leftCurrent{100, 0},
    _rightCurrent{100, 0},
    _leftStart{100, 0},
    _rightStart{100, 0},
    _leftTarget{100, 0},
    _rightTarget{100, 0},
    _transitionFrame(0),
    _transitionFrames(1),
    _mode(MODE_NORMAL),
    _modeStartMillis(0),
    _modeDuration(1),
    _modeLeftFrom{100, 0},
    _modeRightFrom{100, 0},
    _alpha(255),
    _mergeX0(0), _mergeY0(0), _mergeX1(-1), _mergeY1(-1),
    _mergeCapsules(false) {
}

static inline float clamp01(float t) {
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}

void GreetingDisplay::clearFaceArea() {
    int left = _xCenter + EYE_LEFT_OFFSET - EYE_HALF_WIDTH - EYE_MAX_SKEW - FACE_AREA_PADDING;
    int right = _xCenter + EYE_RIGHT_OFFSET + EYE_HALF_WIDTH + EYE_MAX_SKEW + FACE_AREA_PADDING;
    int top = _yCenter + EYE_Y_OFFSET - EYE_HALF_HEIGHT - FACE_AREA_PADDING;
    int bottom = _yCenter + EYE_Y_OFFSET + EYE_HALF_HEIGHT + FACE_AREA_PADDING;
    fillRect(left, top, right - left + 1, bottom - top + 1, BLACK);
}

int GreetingDisplay::eyeHalfHeight(int heightScale) {
    int halfH = (EYE_HALF_HEIGHT * heightScale) / 100;
    return (halfH < 1) ? 1 : halfH;
}

// Horizontal half extent of the eye at row `dy` (relative to the eye center).
// Returns -1 if the row is not part of the eye at all.
int GreetingDisplay::eyeExtent(int heightScale, int dy) {
    int halfH = eyeHalfHeight(heightScale);
    int halfW = EYE_HALF_WIDTH;
    int r = EYE_CORNER_RADIUS;
    if(r > halfH) r = halfH;
    if(r > halfW) r = halfW;

    int ady = (dy < 0) ? -dy : dy;
    if(ady > halfH) return -1;
    if(ady + r <= halfH) return halfW;

    int yy = ady - (halfH - r);
    int dxr = isqrt(r * r - yy * yy);
    return (halfW - r) + dxr;
}

uint16_t GreetingDisplay::eyeRowColor(int heightScale, int dy) {
    int halfH = eyeHalfHeight(heightScale);
    int edge = 0;
    if(halfH >= 6) edge = 3;
    else if(halfH >= 4) edge = 2;
    else if(halfH >= 2) edge = 1;

    if(edge > 0) {
        if(dy <= -halfH + edge - 1) return FACE_EYE_TOP_COLOR;
        if(dy >= halfH - edge + 1) return FACE_EYE_BOTTOM_COLOR;
    }
    return FACE_EYE_COLOR;
}

/**
 * Precomputes the horizontal half extent and the skew offset for every vertical
 * sub-sample of row `dy`. Doing this once per row instead of once per pixel
 * keeps the expensive integer square root out of the inner loop.
 * Extent and offset are relative to the eye center, in 1/AA_SCALE pixel units.
 */
void GreetingDisplay::eyeRowParams(int heightScale, int skew, int dy,
                                   int* extFp, int* offFp) {
    int halfH_fp = (EYE_HALF_HEIGHT * heightScale * AA_SCALE) / 100;
    int halfW_fp = EYE_HALF_WIDTH * AA_SCALE;
    int r_fp = EYE_CORNER_RADIUS * AA_SCALE;

    for(int sy = 0; sy < AA_SUBSAMPLES; sy++) {
        int ys = dy * AA_SCALE + sy * AA_STEP + AA_OFFSET;
        int ady = (ys < 0) ? -ys : ys;
        int ext = -1;

        if(ady <= halfH_fp) {
            if(ady + r_fp <= halfH_fp) {
                ext = halfW_fp;
            } else {
                int yy = ady - (halfH_fp - r_fp);
                if(yy <= r_fp) {
                    ext = (halfW_fp - r_fp) + isqrt(r_fp * r_fp - yy * yy);
                }
            }
        }
        extFp[sy] = ext;
        offFp[sy] = (skew * ys) / EYE_HALF_HEIGHT;
    }
}

// Fraction of the pixel (x, y) covered by the eye, in 1/(AA_SUBSAMPLES^2) units.
int GreetingDisplay::eyeCoverageFast(int x, int xCenter, const int* extFp, const int* offFp) {
    int xsBase = (x - xCenter) * AA_SCALE;
    int count = 0;
    for(int sy = 0; sy < AA_SUBSAMPLES; sy++) {
        int ext = extFp[sy];
        if(ext < 0) continue;
        int off = offFp[sy];
        for(int sx = 0; sx < AA_SUBSAMPLES; sx++) {
            int dx = xsBase + sx * AA_STEP + AA_OFFSET - off;
            if(dx < 0) dx = -dx;
            if(dx <= ext) count++;
        }
    }
    return count;
}

// Largest horizontal half extent of the eye at row `dy` (over the row's sub-samples).
int GreetingDisplay::eyeExtentFp(int heightScale, int dy) {
    int extFp[AA_SUBSAMPLES];
    int offFp[AA_SUBSAMPLES];
    eyeRowParams(heightScale, 0, dy, extFp, offFp);
    int maxExt = -1;
    for(int sy = 0; sy < AA_SUBSAMPLES; sy++) {
        if(extFp[sy] > maxExt) maxExt = extFp[sy];
    }
    return maxExt;
}

/**
 * Draws a single eye as a filled rounded square ("squircle") with anti-aliased
 * edges. heightScale flattens the eye vertically (squint / closed) and skew
 * shears it horizontally (tilted look). The top rows are drawn in a lighter
 * color and the bottom rows in a darker one.
 */
void GreetingDisplay::drawEyeShape(int xCenter, int yCenter, int heightScale, int skew) {
    int halfH = eyeHalfHeight(heightScale);
    int maxCoverage = AA_SUBSAMPLES * AA_SUBSAMPLES;
    int skewAbs = (skew < 0) ? -skew : skew;

    int left = xCenter - EYE_HALF_WIDTH - skewAbs - 1;
    int right = xCenter + EYE_HALF_WIDTH + skewAbs + 1;
    if(left < 0) left = 0;
    if(right >= LCD_WIDTH) right = LCD_WIDTH - 1;

    for(int dy = -halfH - 1; dy <= halfH + 1; dy++) {
        int y = yCenter + dy;
        uint16_t rowColor = eyeRowColor(heightScale, dy);
        int extFp[AA_SUBSAMPLES];
        int offFp[AA_SUBSAMPLES];
        eyeRowParams(heightScale, skew, dy, extFp, offFp);
        EyeRowShader shader;
        eyeRowPrepare(shader, extFp, offFp, AA_SUBSAMPLES, rowColor, maxCoverage);
        if(!shader.any) continue;
        int n = 0;
        int first = -1;
        int last = -1;
        for(int x = left; x <= right; x++) {
            int s0 = (x - xCenter) * AA_SCALE + AA_OFFSET;
            int s1 = s0 + (AA_SUBSAMPLES - 1) * AA_STEP;
            int coverage;
            if(shader.all && s0 >= shader.fullLo && s1 <= shader.fullHi) coverage = maxCoverage;
            else if(s1 < shader.emptyLo || s0 > shader.emptyHi) coverage = 0;
            else coverage = eyeCoverageFast(x, xCenter, extFp, offFp);
            if(coverage == 0) continue;
            if(first < 0) first = x;
            last = x;
            eyeRowBuffer[n++] = shader.palette[coverage];
        }
        if(first >= 0) {
            Display::drawRow(first, y, last, eyeRowBuffer);
        }
    }
}

/**
 * Redraws an eye that changed shape. Only the pixels that can have changed
 * (the rows and columns covered by the old or the new shape, plus a one pixel
 * margin for the anti-aliased fringe) are written, so the eye is never blanked
 * out completely (which would show up as flicker on the panel).
 */
void GreetingDisplay::drawEyeDelta(int xCenter, int yCenter,
                                   int oldHeight, int oldSkew, int newHeight, int newSkew,
                                   uint8_t alpha) {
    int oldHalfH = eyeHalfHeight(oldHeight);
    int newHalfH = eyeHalfHeight(newHeight);
    int maxHalfH = (oldHalfH > newHalfH) ? oldHalfH : newHalfH;
    int maxCoverage = AA_SUBSAMPLES * AA_SUBSAMPLES;

    for(int dy = -maxHalfH - 1; dy <= maxHalfH + 1; dy++) {
        int y = yCenter + dy;

        uint16_t rowColor = Graphics::blend(eyeRowColor(newHeight, dy), alpha);

        int oldExtFp = eyeExtentFp(oldHeight, dy);
        int newExtFp = eyeExtentFp(newHeight, dy);
        if(oldExtFp < 0 && newExtFp < 0) continue;

        // Use the sub-pixel accurate extent so the anti-aliased fringe is covered.
        int extFp = (oldExtFp > newExtFp) ? oldExtFp : newExtFp;
        int extPx = (extFp + AA_SCALE - 1) / AA_SCALE;
        int skewAbs = (oldSkew < 0) ? -oldSkew : oldSkew;
        int newSkewAbs = (newSkew < 0) ? -newSkew : newSkew;
        if(newSkewAbs > skewAbs) skewAbs = newSkewAbs;

        int left = xCenter - extPx - skewAbs - 2;
        int right = xCenter + extPx + skewAbs + 2;
        if(left < 0) left = 0;
        if(right >= LCD_WIDTH) right = LCD_WIDTH - 1;

        int rowExtFp[AA_SUBSAMPLES];
        int rowOffFp[AA_SUBSAMPLES];
        eyeRowParams(newHeight, newSkew, dy, rowExtFp, rowOffFp);
        EyeRowShader shader;
        eyeRowPrepare(shader, rowExtFp, rowOffFp, AA_SUBSAMPLES, rowColor, maxCoverage);

        int n = 0;
        for(int x = left; x <= right; x++) {
            int s0 = (x - xCenter) * AA_SCALE + AA_OFFSET;
            int s1 = s0 + (AA_SUBSAMPLES - 1) * AA_STEP;
            int coverage;
            if(!shader.any) coverage = 0;
            else if(shader.all && s0 >= shader.fullLo && s1 <= shader.fullHi) coverage = maxCoverage;
            else if(s1 < shader.emptyLo || s0 > shader.emptyHi) coverage = 0;
            else coverage = eyeCoverageFast(x, xCenter, rowExtFp, rowOffFp);
            eyeRowBuffer[n++] = shader.palette[coverage];
        }
        Display::drawRow(left, y, right, eyeRowBuffer);
    }
}

void GreetingDisplay::chooseNewExpression() {
    uint32_t choice = get_rand_32() % 10;
    if(choice < 4) {
        _expression = EXPRESSION_NEUTRAL;
    } else if(choice < 6) {
        _expression = EXPRESSION_SQUINT;
    } else if(choice < 7) {
        _expression = EXPRESSION_WINK;
    } else if(choice < 9) {
        _expression = EXPRESSION_TILT;
    } else {
        _expression = EXPRESSION_SLEEP;
    }
}

/**
 * Advances the opening / closing animation. Both eyes are morphed from their
 * start pose to the end pose while their brightness is faded.
 */
void GreetingDisplay::stepModeAnimation(uint32_t now) {
    float t = clamp01((float)(now - _modeStartMillis) / _modeDuration);
    Pose left, right;
    uint8_t alpha;

    if(_mode == MODE_OPENING) {
        // Ease-out with a small overshoot: the eyes pop open like waking up
        const float c1 = 1.3f;
        const float c3 = c1 + 1.0f;
        float u = t - 1.0f;
        float k = 1.0f + c3 * u * u * u + c1 * u * u;
        int height = (int)(100.0f * k + 0.5f);
        if(height < 0) height = 0;
        left.height = right.height = height;
        left.skew = right.skew = 0;
        // Fade in quickly while the eyes are still a thin line
        alpha = (uint8_t)(255.0f * clamp01(t * 3.0f));
    } else {
        // Ease-in: the eyes squeeze shut, the remaining line fades out
        float k = t * t;
        left.height = _modeLeftFrom.height + (int)((0 - _modeLeftFrom.height) * k);
        left.skew = _modeLeftFrom.skew + (int)((0 - _modeLeftFrom.skew) * k);
        right.height = _modeRightFrom.height + (int)((0 - _modeRightFrom.height) * k);
        right.skew = _modeRightFrom.skew + (int)((0 - _modeRightFrom.skew) * k);
        alpha = (uint8_t)(255.0f * clamp01((1.0f - t) * 2.5f));
    }

    if(left.height != _leftCurrent.height || left.skew != _leftCurrent.skew ||
       right.height != _rightCurrent.height || right.skew != _rightCurrent.skew ||
       alpha != _alpha) {
        int eyeY = _yCenter + EYE_Y_OFFSET;
        drawEyeDelta(_xCenter + EYE_LEFT_OFFSET, eyeY,
                     _leftCurrent.height, _leftCurrent.skew, left.height, left.skew, alpha);
        drawEyeDelta(_xCenter + EYE_RIGHT_OFFSET, eyeY,
                     _rightCurrent.height, _rightCurrent.skew, right.height, right.skew, alpha);
        _leftCurrent = left;
        _rightCurrent = right;
        _alpha = alpha;
    }

    if(t >= 1.0f) {
        if(_mode == MODE_CLOSING) {
            clearFaceArea();
            _hidden = true;
            _mode = MODE_NORMAL;
        } else {
            // Fully open: continue with the normal idle animation
            _mode = MODE_NORMAL;
            _alpha = 255;
            resetIdleAnimation();
        }
    }
}

/**
 * Resets blinking and expressions so the idle animation starts from the
 * neutral, fully open pose.
 */
void GreetingDisplay::resetIdleAnimation() {
    _isBlinking = false;
    _blinkStartMillis = 0;
    _nextBlinkDelay = 0;
    _lastBlinkMillis = 0;
    _expression = EXPRESSION_NEUTRAL;
    _lastExpressionMillis = millis();
    _nextExpressionDelay = EXPRESSION_INTERVAL_MIN_MS + (get_rand_32() % (EXPRESSION_INTERVAL_MAX_MS - EXPRESSION_INTERVAL_MIN_MS + 1));
    _leftStart = _leftCurrent;
    _rightStart = _rightCurrent;
    _leftTarget.height = 100;
    _leftTarget.skew = 0;
    _rightTarget.height = 100;
    _rightTarget.skew = 0;
    // Glide from the current pose (e.g. after the opening overshoot) to neutral
    _transitionFrame = 0;
    _transitionFrames = EXPRESSION_TRANSITION_FRAMES;
}

void GreetingDisplay::stepAnimation() {
    uint32_t now = millis();
    if(now - _lastAnimationMillis < ANIMATION_INTERVAL_MS) {
        return;
    }
    _lastAnimationMillis = now;

    if(_mode == MODE_MERGING) {
        stepMergeAnimation(now);
        return;
    }
    if(_mode != MODE_NORMAL) {
        stepModeAnimation(now);
        return;
    }

    // --- blink state ---
    if(_isBlinking) {
        bool fullyClosed = _leftCurrent.height <= EYE_BLINK_HEIGHT && _rightCurrent.height <= EYE_BLINK_HEIGHT;
        if(fullyClosed && now - _blinkStartMillis >= BLINK_MIN_DURATION_MS) {
            _isBlinking = false;
            _nextBlinkDelay = BLINK_INTERVAL_MIN_MS + (get_rand_32() % (BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS + 1));
            _lastBlinkMillis = now;
        }
    } else {
        if(_nextBlinkDelay == 0) {
            _nextBlinkDelay = BLINK_INTERVAL_MIN_MS + (get_rand_32() % (BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS + 1));
            _lastBlinkMillis = now;
        } else if(now - _lastBlinkMillis >= _nextBlinkDelay) {
            _isBlinking = true;
            _blinkStartMillis = now;
        }
    }

    // --- expression state ---
    if(_nextExpressionDelay == 0 || now - _lastExpressionMillis >= _nextExpressionDelay) {
        chooseNewExpression();
        _lastExpressionMillis = now;
        _nextExpressionDelay = EXPRESSION_INTERVAL_MIN_MS + (get_rand_32() % (EXPRESSION_INTERVAL_MAX_MS - EXPRESSION_INTERVAL_MIN_MS + 1));
    }

    // --- target pose of both eyes ---
    Pose leftTarget;
    Pose rightTarget;
    leftTarget.height = 100;
    leftTarget.skew = 0;
    rightTarget.height = 100;
    rightTarget.skew = 0;

    if(_isBlinking) {
        leftTarget.height = EYE_BLINK_HEIGHT;
        rightTarget.height = EYE_BLINK_HEIGHT;
    } else {
        switch(_expression) {
            case EXPRESSION_SQUINT:
                leftTarget.height = 62;
                rightTarget.height = 62;
                break;

            case EXPRESSION_WINK:
                leftTarget.height = EYE_BLINK_HEIGHT;
                break;

            case EXPRESSION_TILT:
                leftTarget.height = 88;
                rightTarget.height = 88;
                leftTarget.skew = EYE_MAX_SKEW;
                rightTarget.skew = -EYE_MAX_SKEW;
                break;

            case EXPRESSION_SLEEP:
                leftTarget.height = EYE_BLINK_HEIGHT;
                rightTarget.height = EYE_BLINK_HEIGHT;
                break;

            default:
                break;
        }
    }

    // --- start a new transition whenever the target pose changes ---
    bool targetChanged = leftTarget.height != _leftTarget.height || leftTarget.skew != _leftTarget.skew ||
                         rightTarget.height != _rightTarget.height || rightTarget.skew != _rightTarget.skew;

    if(targetChanged) {
        _leftStart = _leftCurrent;
        _rightStart = _rightCurrent;
        _leftTarget = leftTarget;
        _rightTarget = rightTarget;
        _transitionFrame = 0;
        // A blink (closing and reopening) is quick, expression changes are gentle.
        bool quick = _isBlinking ||
                     (_leftCurrent.height <= EYE_BLINK_HEIGHT && _rightCurrent.height <= EYE_BLINK_HEIGHT);
        _transitionFrames = quick ? BLINK_TRANSITION_FRAMES : EXPRESSION_TRANSITION_FRAMES;
    }

    // --- linear interpolation between the start and target pose ---
    int oldLeftHeight = _leftCurrent.height;
    int oldLeftSkew = _leftCurrent.skew;
    int oldRightHeight = _rightCurrent.height;
    int oldRightSkew = _rightCurrent.skew;

    bool moved = false;
    if(_transitionFrame < _transitionFrames) {
        _transitionFrame++;
        _leftCurrent.height = _leftStart.height +
            ((_leftTarget.height - _leftStart.height) * _transitionFrame) / _transitionFrames;
        _leftCurrent.skew = _leftStart.skew +
            ((_leftTarget.skew - _leftStart.skew) * _transitionFrame) / _transitionFrames;
        _rightCurrent.height = _rightStart.height +
            ((_rightTarget.height - _rightStart.height) * _transitionFrame) / _transitionFrames;
        _rightCurrent.skew = _rightStart.skew +
            ((_rightTarget.skew - _rightStart.skew) * _transitionFrame) / _transitionFrames;
        moved = true;
    }

    if(moved) {
        int eyeY = _yCenter + EYE_Y_OFFSET;
        drawEyeDelta(_xCenter + EYE_LEFT_OFFSET, eyeY,
                     oldLeftHeight, oldLeftSkew, _leftCurrent.height, _leftCurrent.skew);
        drawEyeDelta(_xCenter + EYE_RIGHT_OFFSET, eyeY,
                     oldRightHeight, oldRightSkew, _rightCurrent.height, _rightCurrent.skew);
    }
}

void GreetingDisplay::showRandom(uint16_t color) {
    (void)color; // the eye colors are configured in config.h

    if(!_hidden) {
        hide();
    }

    _hidden = false;
    _mode = MODE_NORMAL;
    _alpha = 255;
    _isBlinking = false;
    _blinkStartMillis = 0;
    _nextBlinkDelay = 0;
    _lastBlinkMillis = 0;
    _lastAnimationMillis = 0;
    _expression = EXPRESSION_NEUTRAL;
    _lastExpressionMillis = millis();
    _nextExpressionDelay = EXPRESSION_INTERVAL_MIN_MS + (get_rand_32() % (EXPRESSION_INTERVAL_MAX_MS - EXPRESSION_INTERVAL_MIN_MS + 1));
    _leftCurrent.height = 100;
    _leftCurrent.skew = 0;
    _rightCurrent.height = 100;
    _rightCurrent.skew = 0;
    _leftStart = _leftCurrent;
    _rightStart = _rightCurrent;
    _leftTarget = _leftCurrent;
    _rightTarget = _rightCurrent;
    _transitionFrame = 0;
    _transitionFrames = 1;

    int eyeY = _yCenter + EYE_Y_OFFSET;
    drawEyeShape(_xCenter + EYE_LEFT_OFFSET, eyeY, _leftCurrent.height, _leftCurrent.skew);
    drawEyeShape(_xCenter + EYE_RIGHT_OFFSET, eyeY, _rightCurrent.height, _rightCurrent.skew);
}

void GreetingDisplay::hide() {
    _mode = MODE_NORMAL;
    _alpha = 255;
    if(_hidden) return;
    clearFaceArea();
    _hidden = true;
}

void GreetingDisplay::open(uint32_t durationMs) {
    if(!_hidden) hide();

    _hidden = false;
    _mode = MODE_OPENING;
    _modeStartMillis = millis();
    _modeDuration = durationMs > 0 ? durationMs : 1;
    _lastAnimationMillis = 0;
    // Start invisible, as a closed line
    _leftCurrent.height = 0;
    _leftCurrent.skew = 0;
    _rightCurrent = _leftCurrent;
    _alpha = 0;
}

void GreetingDisplay::merge(uint32_t durationMs) {
    if(_hidden || _mode == MODE_MERGING) return;

    _mode = MODE_MERGING;
    _modeStartMillis = millis();
    _modeDuration = durationMs > 0 ? durationMs : 1;
    _lastAnimationMillis = 0;
    _modeLeftFrom = _leftCurrent;
    _modeRightFrom = _rightCurrent;
    _mergeCapsules = false;
}

/**
 * Draws two horizontal capsules ("pill" shapes with round ends) centered at
 * (xLeft, y) and (xRight, y) with anti-aliased edges. The whole area of the
 * previous frame and the new capsules is rewritten, so no black frame is needed.
 */
void GreetingDisplay::drawCapsules(float xLeft, float xRight, float y, float halfLength,
                                   float radius, uint8_t alpha) {
    // Bounding box of the new capsules
    int nx0 = (int)floorf(xLeft - halfLength) - 2;
    int nx1 = (int)ceilf(xRight + halfLength) + 2;
    int ny0 = (int)floorf(y - radius) - 2;
    int ny1 = (int)ceilf(y + radius) + 2;

    // Redraw the union with the previous frame's area
    int x0 = nx0 < _mergeX0 ? nx0 : _mergeX0;
    int x1 = nx1 > _mergeX1 ? nx1 : _mergeX1;
    int y0 = ny0 < _mergeY0 ? ny0 : _mergeY0;
    int y1 = ny1 > _mergeY1 ? ny1 : _mergeY1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if(y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    // Straight middle part of each capsule
    float straight = halfLength - radius;
    if(straight < 0) straight = 0;
    uint16_t color = Graphics::blend(FACE_EYE_COLOR, alpha);

    // Squared distances that are surely inside / outside: the square root
    // is only needed for the one pixel wide anti-aliased edge
    float inner = radius - 0.5f;
    float outer = radius + 0.5f;
    float inner2 = inner > 0 ? inner * inner : 0;
    float outer2 = outer * outer;

    for(int py = y0; py <= y1; py++) {
        float dy = py + 0.5f - y;
        float dy2 = dy * dy;
        if(dy2 >= outer2) {
            // Row completely outside of the capsules: just clear it
            for(int px = x0; px <= x1; px++) eyeRowBuffer[px - x0] = BLACK;
            Display::drawRow(x0, py, x1, eyeRowBuffer);
            continue;
        }
        for(int px = x0; px <= x1; px++) {
            float coverage = 0;
            float centers[2] = { xLeft, xRight };
            for(int i = 0; i < 2; i++) {
                float dx = fabsf(px + 0.5f - centers[i]) - straight;
                if(dx < 0) dx = 0;
                float d2 = dx*dx + dy2;
                float c;
                if(d2 >= outer2) c = 0;
                else if(d2 <= inner2) c = 1;
                else c = 0.5f - (sqrtf(d2) - radius);
                if(c > coverage) coverage = c;
                if(coverage >= 1) break;
            }
            if(coverage > 1) coverage = 1;
            eyeRowBuffer[px - x0] = (coverage <= 0)
                ? BLACK
                : Graphics::blend(color, (uint8_t)(coverage * 255.0f + 0.5f));
        }
        Display::drawRow(x0, py, x1, eyeRowBuffer);
    }

    _mergeX0 = nx0;
    _mergeX1 = nx1;
    _mergeY0 = ny0;
    _mergeY1 = ny1;
}

/**
 * Merge animation in three phases:
 *  1. both eyes squeeze into thin lines (shape morph of the normal eyes)
 *  2. the lines slide to the center and shrink until they meet as one dot
 *  3. the dot glows up a little and fades out
 */
void GreetingDisplay::stepMergeAnimation(uint32_t now) {
    float t = clamp01((float)(now - _modeStartMillis) / _modeDuration);
    int eyeY = _yCenter + EYE_Y_OFFSET;

    if(t < MERGE_SQUINT_SHARE) {
        // Phase 1: squeeze into lines (ease-in)
        float k = t / MERGE_SQUINT_SHARE;
        k = k * k;
        Pose left, right;
        left.height = _modeLeftFrom.height + (int)((MERGE_LINE_HEIGHT - _modeLeftFrom.height) * k);
        left.skew = _modeLeftFrom.skew + (int)((0 - _modeLeftFrom.skew) * k);
        right.height = _modeRightFrom.height + (int)((MERGE_LINE_HEIGHT - _modeRightFrom.height) * k);
        right.skew = _modeRightFrom.skew + (int)((0 - _modeRightFrom.skew) * k);
        if(left.height != _leftCurrent.height || left.skew != _leftCurrent.skew ||
           right.height != _rightCurrent.height || right.skew != _rightCurrent.skew) {
            drawEyeDelta(_xCenter + EYE_LEFT_OFFSET, eyeY,
                         _leftCurrent.height, _leftCurrent.skew, left.height, left.skew);
            drawEyeDelta(_xCenter + EYE_RIGHT_OFFSET, eyeY,
                         _rightCurrent.height, _rightCurrent.skew, right.height, right.skew);
            _leftCurrent = left;
            _rightCurrent = right;
        }
        return;
    }

    if(!_mergeCapsules) {
        // First capsule frame: overwrite the whole face area once, so nothing
        // of the eye shapes (e.g. their colored top/bottom rows) stays behind
        _mergeX0 = _xCenter + EYE_LEFT_OFFSET - EYE_HALF_WIDTH - EYE_MAX_SKEW - FACE_AREA_PADDING;
        _mergeX1 = _xCenter + EYE_RIGHT_OFFSET + EYE_HALF_WIDTH + EYE_MAX_SKEW + FACE_AREA_PADDING;
        _mergeY0 = eyeY - EYE_HALF_HEIGHT - FACE_AREA_PADDING;
        _mergeY1 = eyeY + EYE_HALF_HEIGHT + FACE_AREA_PADDING;
        _mergeCapsules = true;
    }

    // Eye centers (pixel centers) at the start and the meeting point
    float xLeftStart = _xCenter + EYE_LEFT_OFFSET + 0.5f;
    float xRightStart = _xCenter + EYE_RIGHT_OFFSET + 0.5f;
    float xMeet = _xCenter;
    float y = eyeY + 0.5f;
    // Start with the length of the squeezed eye so the switch is seamless
    float halfLengthStart = (float)eyeExtentFp(MERGE_LINE_HEIGHT, 0) / AA_SCALE + 0.5f;

    float slideEnd = MERGE_SQUINT_SHARE + MERGE_SLIDE_SHARE;
    if(t < slideEnd) {
        // Phase 2: slide together (ease-in-out) while shrinking to a dot
        float k = (t - MERGE_SQUINT_SHARE) / MERGE_SLIDE_SHARE;
        k = (k < 0.5f) ? 4*k*k*k : 1 - (-2*k + 2)*(-2*k + 2)*(-2*k + 2) / 2;
        float xLeft = xLeftStart + (xMeet - xLeftStart) * k;
        float xRight = xRightStart + (xMeet - xRightStart) * k;
        float halfLength = halfLengthStart + (MERGE_MEET_HALF_LENGTH - halfLengthStart) * k;
        drawCapsules(xLeft, xRight, y, halfLength, MERGE_LINE_RADIUS, 255);
        return;
    }

    // Phase 3: the short line rounds up into the pen dot (ease-out)
    float k = (t - slideEnd) / (1.0f - slideEnd);
    float u = 1 - k;
    float grow = 1 - u * u * u;
    float radius = MERGE_LINE_RADIUS + (PEN_DOT_RADIUS - MERGE_LINE_RADIUS) * grow;
    float halfLength = MERGE_MEET_HALF_LENGTH + (radius - MERGE_MEET_HALF_LENGTH) * grow;
    if(halfLength < radius) halfLength = radius;
    drawCapsules(xMeet, xMeet, y, halfLength, radius, 255);

    if(t >= 1.0f) {
        // The dot stays on the display, the counter's pen takes it over
        _hidden = true;
        _mode = MODE_NORMAL;
        _alpha = 255;
        _mergeCapsules = false;
    }
}

void GreetingDisplay::mergeDotPosition(float& x, float& y) const {
    x = _xCenter;
    y = _yCenter + EYE_Y_OFFSET + 0.5f;
}

void GreetingDisplay::clearMergeDot() {
    if(_mergeX1 >= _mergeX0) {
        Graphics::fillRect(_mergeX0, _mergeY0, _mergeX1, _mergeY1, BLACK);
    }
}

void GreetingDisplay::close(uint32_t durationMs) {
    if(_hidden || _mode == MODE_CLOSING) return;

    _mode = MODE_CLOSING;
    _modeStartMillis = millis();
    _modeDuration = durationMs > 0 ? durationMs : 1;
    _lastAnimationMillis = 0;
    _modeLeftFrom = _leftCurrent;
    _modeRightFrom = _rightCurrent;
}

void GreetingDisplay::animateEyes() {
    if(_hidden) {
        return;
    }
    stepAnimation();
}
