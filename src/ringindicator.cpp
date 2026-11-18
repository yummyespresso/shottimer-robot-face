#include <ringindicator.h>
#include <graphics.h>
#include <Arduino.h>
#include <math.h>
#include <serialutils.h>
#include <config.h>
#include <display.h>

// Extra margin (in pixels) around the ring band for the anti-aliased fringe.
static constexpr float RING_AA_MARGIN = 1.5f;

// Glowing head at the end of the fill (timer style)
static constexpr float HEAD_CORE_RADIUS = 4.0f;
static constexpr float HEAD_GLOW_RADIUS = 11.0f;
static constexpr float HEAD_GLOW_STRENGTH = 0.45f;
// How much the head is lightened towards white (0..255)
static constexpr uint8_t HEAD_WHITENESS = 150;

static void buildRingLut();

static inline float clamp01(float t) {
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}

// --- Color gradient helpers -------------------------------------------------

// Interpolates two colors in linear light (gamma ~2). Compared to a plain RGB
// mix the transition does not get darker in the middle, compared to a mix in
// HSV it does not pass through other hues (no bright cyan between blue and
// green, no red between magenta and yellow).
static uint16_t mixLinear(uint16_t a, uint16_t b, float t) {
    float ar = ((a >> 11) & 0x1F) / 31.0f, ag = ((a >> 5) & 0x3F) / 63.0f, ab = (a & 0x1F) / 31.0f;
    float br = ((b >> 11) & 0x1F) / 31.0f, bg = ((b >> 5) & 0x3F) / 63.0f, bb = (b & 0x1F) / 31.0f;
    float r = sqrtf(ar*ar + (br*br - ar*ar) * t);
    float g = sqrtf(ag*ag + (bg*bg - ag*ag) * t);
    float bl = sqrtf(ab*ab + (bb*bb - ab*ab) * t);
    uint16_t r5 = (uint16_t)(r * 31.0f + 0.5f);
    uint16_t g6 = (uint16_t)(g * 63.0f + 0.5f);
    uint16_t b5 = (uint16_t)(bl * 31.0f + 0.5f);
    return (r5 << 11) | (g6 << 5) | b5;
}

// -----------------------------------------------------------------------------

RingIndicator::RingIndicator()
    : _color(RING_COLOR_OK), _xCenter(RING_CENTER_X), _yCenter(RING_CENTER_Y), _radius(RING_RADIUS), _width(RING_WIDTH) {

    // Default to full circle
    _radMin = 0.0;
    _radMax = 2*PI;
    _hidden = true;

    _radValue = _radMin;
    _radOffset = 0.0;

    _timerStyle = false;
    _trackValue = _radMin;
    _head = false;
    _fillVisible = true;
    for(int i = 0; i < GRADIENT_SIZE; i++) _gradient[i] = RING_COLOR_TIMER_START;

    // Prepare the ring's look-up table and the color gradient at boot, so the
    // first frame of an animation does not have to wait for them
    buildRingLut();
    buildGradient();
}

void RingIndicator::setLimits(float radMin, float radMax) {
    if(radMin > radMax) {
        sout.warn() <= "setLimits: Refusing to set limits with min > max";
        return;
    }

    hide();
    _radMin = radMin;
    _radMax = radMax;
    _radValue = _radMin;
    _trackValue = _radMin;
};

void RingIndicator::setOffset(float radOffset) {
    hide();
    _radOffset = radOffset;
}

void RingIndicator::setTimerStyle(bool timerStyle) {
    hide();
    _timerStyle = timerStyle;
}

void RingIndicator::buildGradient() {
    // Color gradient along the ring, defined by stops in seconds
    struct Stop { float sec; uint16_t color; };
    float windowStart = TARGET_WINDOW_START_SEC;
    float windowEnd = TARGET_WINDOW_END_SEC;
    // The start color slowly turns into RING_COLOR_TIMER until shortly before
    // the window, then it blends into green; after the window it goes over
    // yellow to orange/red
    float approach = windowStart - 1.5f;
    if(approach < 0) approach = 0;
    const Stop stops[] = {
        { 0.0f,                 RING_COLOR_TIMER_START },
        { approach,             RING_COLOR_TIMER },
        { windowStart,          RING_COLOR_TIMER_COMPLETE },
        { windowEnd,            RING_COLOR_TIMER_COMPLETE },
        { windowEnd + 1.0f,     RING_COLOR_TIMER_WARN },
        { windowEnd + 2.0f,     RING_COLOR_TIMER_OVER },
        { TIMER_RING_MAX_SEC,   RING_COLOR_TIMER_LATE },
    };
    const int nStops = sizeof(stops) / sizeof(stops[0]);

    for(int i = 0; i < GRADIENT_SIZE; i++) {
        float sec = (float)i / (GRADIENT_SIZE - 1) * TIMER_RING_MAX_SEC;
        uint16_t color = stops[nStops - 1].color;
        for(int j = 0; j < nStops - 1; j++) {
            if(sec <= stops[j + 1].sec) {
                float span = stops[j + 1].sec - stops[j].sec;
                float t = span > 0 ? (sec - stops[j].sec) / span : 1.0f;
                color = mixLinear(stops[j].color, stops[j + 1].color, clamp01(t));
                break;
            }
        }
        _gradient[i] = color;
    }
}

// --- Look-up table of the ring band ------------------------------------------
//
// The RP2040 has no floating point unit, so atan2 and sqrt per pixel are far
// too slow for 60 FPS. The angle and the distance to the ring center of every
// pixel of the ring band never change, so they are computed once and stored:
//   lutAngle:  angle in units of 2*PI/65536 (0 = bottom, like sin/cos above)
//   lutRadius: distance to the ring center in 1/64 pixel
// The pixels are stored row by row, each row has up to two spans (left and
// right part of the ring).

// Units of a full turn
static constexpr int32_t TURN = 65536;
// Fixed point scale of radii and distances
static constexpr int32_t RQ = 64;
// Fixed point scale of positions (cap centers)
static constexpr int32_t PQ = 16;

struct RingSpan {
    int16_t y;
    int16_t x0;
    int16_t x1;
    uint16_t offset;
};

static constexpr int RING_LUT_MAX_SPANS = 2 * LCD_HEIGHT;
static constexpr int RING_LUT_MAX_PIXELS = 20000;
static RingSpan lutSpans[RING_LUT_MAX_SPANS];
static int lutSpanCount = 0;
static uint16_t lutAngle[RING_LUT_MAX_PIXELS];
static uint16_t lutRadius[RING_LUT_MAX_PIXELS];
static bool lutReady = false;

// Distance the band reaches beyond the ring's edge: anti-aliasing plus the
// glow of the head
static float bandMargin() {
    float glowMargin = HEAD_GLOW_RADIUS - RING_WIDTH * 0.5f + RING_AA_MARGIN;
    return glowMargin > RING_AA_MARGIN ? glowMargin : RING_AA_MARGIN;
}

static void buildRingLut() {
    if(lutReady) return;
    float rIn = RING_RADIUS - RING_WIDTH * 0.5f - bandMargin();
    float rOut = RING_RADIUS + RING_WIDTH * 0.5f + bandMargin();
    float rIn2 = rIn > 0 ? rIn * rIn : 0;
    float rOut2 = rOut * rOut;
    int count = 0;

    for(int y = 0; y < LCD_HEIGHT; y++) {
        float dy = y + 0.5f - RING_CENTER_Y;
        int spanStart = -1;
        for(int x = 0; x <= LCD_WIDTH; x++) {
            bool inBand = false;
            if(x < LCD_WIDTH) {
                float dx = x + 0.5f - RING_CENTER_X;
                float r2 = dx*dx + dy*dy;
                inBand = r2 >= rIn2 && r2 <= rOut2;
            }
            if(inBand && spanStart < 0) {
                spanStart = x;
            } else if(!inBand && spanStart >= 0) {
                if(lutSpanCount < RING_LUT_MAX_SPANS && count + (x - spanStart) <= RING_LUT_MAX_PIXELS) {
                    RingSpan& span = lutSpans[lutSpanCount++];
                    span.y = y;
                    span.x0 = spanStart;
                    span.x1 = x - 1;
                    span.offset = count;
                    for(int px = spanStart; px < x; px++) {
                        float dx = px + 0.5f - RING_CENTER_X;
                        float angle = atan2f(dx, dy);
                        if(angle < 0) angle += 2 * PI;
                        lutAngle[count] = (uint16_t)((int32_t)(angle / (2 * PI) * TURN) & 0xFFFF);
                        lutRadius[count] = (uint16_t)(sqrtf(dx*dx + dy*dy) * RQ + 0.5f);
                        count++;
                    }
                }
                spanStart = -1;
            }
        }
    }
    lutReady = true;
}

// Converts an angle in radiants to units (any sign, not wrapped)
static inline int32_t toUnits(float rad) {
    return (int32_t)lroundf(rad / (2 * PI) * TURN);
}

// Coverage (0..255) of a pixel whose center is d (in 1/RQ pixel) away from
// the edge of a shape (negative = inside), with a one pixel wide edge
static inline int32_t coverageFromDistance(int32_t d) {
    int32_t c = RQ / 2 - d;
    if(c <= 0) return 0;
    if(c >= RQ) return 255;
    return (c * 255) / RQ;
}

static inline int32_t isqrt32(int32_t n) {
    if(n <= 0) return 0;
    int32_t x = (int32_t)sqrtf((float)n);
    // correct rounding errors of the float estimate
    while(x * x > n) x--;
    while((x + 1) * (x + 1) <= n) x++;
    return x;
}

// Distance (1/RQ pixel) of pixel (x, y) to the nearer of two points given in
// 1/PQ pixel, minus halfWidth (1/RQ pixel). Returns a large value if the
// pixel is further away than limit (1/RQ pixel) from both points, which
// saves the square root for most pixels.
static inline int32_t capDistance(int x, int y, int32_t ax, int32_t ay, int32_t bx, int32_t by,
                                  int32_t halfWidth, int32_t limit) {
    int32_t px = x * PQ + PQ / 2, py = y * PQ + PQ / 2;
    int32_t dax = px - ax, day = py - ay, dbx = px - bx, dby = py - by;
    int32_t da = dax*dax + day*day;
    int32_t db = dbx*dbx + dby*dby;
    int32_t d2 = da < db ? da : db;              // in 1/PQ^2 pixel^2
    int32_t limitPq = (limit * PQ) / RQ;
    if(d2 > limitPq * limitPq) return limit;
    // sqrt in 1/PQ pixel, then convert to 1/RQ pixel
    return (isqrt32(d2) * RQ) / PQ - halfWidth;
}

// -----------------------------------------------------------------------------

void RingIndicator::prepareGeometry() {
    buildRingLut();

    float arcStart = _radMin + _radOffset;
    _startU = (uint16_t)(toUnits(arcStart) & 0xFFFF);
    _spanU = toUnits(_radValue - _radMin);
    _trackSpanU = toUnits(_trackValue - _radMin);
    _fullSpanU = toUnits(_radMax - _radMin);
    if(_fullSpanU < 1) _fullSpanU = 1;
    _windowStartU = (int32_t)((float)TARGET_WINDOW_START_SEC / TIMER_RING_MAX_SEC * _fullSpanU);
    _windowEndU = (int32_t)((float)TARGET_WINDOW_END_SEC / TIMER_RING_MAX_SEC * _fullSpanU);
    // posQ8 = t * scale >> 15 maps [0, fullSpan] to [0, (GRADIENT_SIZE-1)*256]
    _gradientScale = (int32_t)((float)(GRADIENT_SIZE - 1) * 256.0f * 32768.0f / _fullSpanU);

    float arcEnd = _radValue + _radOffset;
    float trackEnd = _trackValue + _radOffset;
    // 0 rad is at the bottom of the circle: x = sin, y = cos
    _capSX = (int32_t)lroundf((_xCenter + sinf(arcStart) * _radius) * PQ);
    _capSY = (int32_t)lroundf((_yCenter + cosf(arcStart) * _radius) * PQ);
    _capEX = (int32_t)lroundf((_xCenter + sinf(arcEnd) * _radius) * PQ);
    _capEY = (int32_t)lroundf((_yCenter + cosf(arcEnd) * _radius) * PQ);
    _capTX = (int32_t)lroundf((_xCenter + sinf(trackEnd) * _radius) * PQ);
    _capTY = (int32_t)lroundf((_yCenter + cosf(trackEnd) * _radius) * PQ);
    if(_timerStyle) {
        _headColor = Graphics::mix(gradientAt(_spanU), WHITE, HEAD_WHITENESS);
        _trackColor = Graphics::blend(RING_TRACK_COLOR, RING_TRACK_ALPHA);
    }
}

uint16_t RingIndicator::gradientAt(int32_t t) {
    if(t <= 0) return _gradient[0];
    if(t >= _fullSpanU) return _gradient[GRADIENT_SIZE - 1];
    uint32_t pos = ((uint32_t)t * (uint32_t)_gradientScale) >> 15;
    uint32_t i = pos >> 8;
    if(i >= GRADIENT_SIZE - 1) return _gradient[GRADIENT_SIZE - 1];
    return Graphics::mix(_gradient[i], _gradient[i + 1], (uint8_t)(pos & 0xFF));
}

uint16_t RingIndicator::pixelColor(int x, int y, uint16_t angle, uint16_t radius) {
    if(_hidden) return BLACK;

    const int32_t radius64 = _radius * RQ;
    const int32_t halfWidth = _width * RQ / 2;
    int32_t centerDist = (int32_t)radius - radius64;
    if(centerDist < 0) centerDist = -centerDist;
    bool headGlow = _timerStyle && _head && _fillVisible;

    // Angle relative to the start of the arc in [0, TURN)
    int32_t t = (uint16_t)(angle - _startU);
    // Pixels beyond the far half count as "before the start"
    const int32_t limit = 2 * RQ;

    // Distance (1/RQ pixel) to an arc of half width hw from the start to
    // span with rounded ends (end cap at ex, ey)
    auto arcDist = [&](int32_t span, int32_t ex, int32_t ey, int32_t hw) -> int32_t {
        if(t <= span) return centerDist - hw;
        return capDistance(x, y, _capSX, _capSY, ex, ey, hw, hw + limit);
    };

    if(!_timerStyle) {
        if(centerDist - halfWidth >= RQ / 2) return BLACK;
        int32_t coverage = coverageFromDistance(arcDist(_spanU, _capEX, _capEY, halfWidth));
        if(coverage <= 0) return BLACK;
        if(coverage >= 255) return _color;
        return Graphics::blend(_color, (uint8_t)coverage);
    }

    // --- Timer style ---
    uint16_t color = BLACK;
    // Angle clamped onto an arc for its color: pixels just before the start
    // (t close to a full turn) belong to the start, pixels beyond the end to
    // the end
    auto clampToArc = [&](int32_t span) -> int32_t {
        if(t <= span) return t;
        return (t > TURN / 2 + span / 2) ? 0 : span;
    };

    if(centerDist - halfWidth < RQ / 2) {
        // Track: neutral gray, pale dark green inside the target window
        const int32_t trackHalfWidth = RING_TRACK_WIDTH * RQ / 2;
        int32_t trackCoverage = coverageFromDistance(arcDist(_trackSpanU, _capTX, _capTY, trackHalfWidth));
        if(trackCoverage > 0) {
            int32_t tTrack = clampToArc(_trackSpanU);
            bool inWindow = tTrack >= _windowStartU && tTrack <= _windowEndU;
            uint16_t trackColor = inWindow ? (uint16_t)RING_TRACK_WINDOW_COLOR : _trackColor;
            color = Graphics::blend(trackColor, (uint8_t)trackCoverage);
        }

        // Fill: gradient color along the ring
        if(_fillVisible) {
            int32_t fillCoverage = coverageFromDistance(arcDist(_spanU, _capEX, _capEY, halfWidth));
            if(fillCoverage > 0) {
                uint16_t fillColor = gradientAt(clampToArc(_spanU));
                color = Graphics::mix(color, fillColor, (uint8_t)fillCoverage);
            }
        }
    }

    // Glowing head at the end of the fill
    if(headGlow) {
        int32_t hx = x * PQ + PQ / 2 - _capEX, hy = y * PQ + PQ / 2 - _capEY;
        int32_t d2 = hx*hx + hy*hy;
        const int32_t glowR = (int32_t)(HEAD_GLOW_RADIUS * PQ);
        if(d2 < glowR * glowR) {
            int32_t d = isqrt32(d2 * 16);                       // 1/64 pixel
            int32_t core = coverageFromDistance(d - (int32_t)(HEAD_CORE_RADIUS * RQ));
            int32_t glow = (int32_t)(HEAD_GLOW_STRENGTH * 255.0f) *
                           ((int32_t)(HEAD_GLOW_RADIUS * RQ) - d) / (int32_t)(HEAD_GLOW_RADIUS * RQ);
            int32_t k = core > glow ? core : glow;
            if(k > 0) color = Graphics::mix(color, _headColor, (uint8_t)k);
        }
    }

    return color;
}

void RingIndicator::renderAngles(float radFrom, float radTo) {
    if(radTo < radFrom) {
        float tmp = radFrom;
        radFrom = radTo;
        radTo = tmp;
    }

    prepareGeometry();

    // The rounded ends and the head's glow stick out by up to this angle
    float innerRadius = _radius - _width * 0.5f - bandMargin();
    float padRad = (_width * 0.5f + bandMargin() + 1.0f) / innerRadius;
    float from = radFrom - padRad;
    float to = radTo + padRad;

    // Angular filter: pixel angles in [fromU, fromU + rangeU]
    uint16_t fromU = (uint16_t)(toUnits(from) & 0xFFFF);
    int32_t rangeU = toUnits(to - from);
    bool everything = rangeU >= TURN;

    // Rows to scan: bounding box of the angle range (sampled)
    float yMinF = 1e9f, yMaxF = -1e9f;
    if(everything || (to - from) > PI) {
        yMinF = 0;
        yMaxF = LCD_HEIGHT - 1;
    } else {
        for(float a = from; ; a += 0.2f) {
            if(a > to) a = to;
            float yc = cosf(a);
            if(yc < yMinF) yMinF = yc;
            if(yc > yMaxF) yMaxF = yc;
            if(a >= to) break;
        }
        float outer = _radius + _width * 0.5f + bandMargin();
        float inner = innerRadius;
        // cos of the extremes, scaled with the inner or outer radius
        yMinF = _yCenter + (yMinF < 0 ? yMinF * outer : yMinF * inner) - 1;
        yMaxF = _yCenter + (yMaxF > 0 ? yMaxF * outer : yMaxF * inner) + 1;
    }
    int yMin = (int)yMinF, yMax = (int)ceilf(yMaxF);

    for(int s = 0; s < lutSpanCount; s++) {
        const RingSpan& span = lutSpans[s];
        if(span.y < yMin || span.y > yMax) continue;
        int runStart = -1;
        int n = 0;
        for(int x = span.x0; x <= span.x1 + 1; x++) {
            bool inRange = false;
            uint16_t angle = 0, radius = 0;
            if(x <= span.x1) {
                int idx = span.offset + (x - span.x0);
                angle = lutAngle[idx];
                radius = lutRadius[idx];
                inRange = everything || (int32_t)(uint16_t)(angle - fromU) <= rangeU;
            }
            if(inRange) {
                if(runStart < 0) runStart = x;
                Graphics::rowBuffer[n++] = pixelColor(x, span.y, angle, radius);
            } else if(runStart >= 0) {
                // Write the finished run of pixels in one transfer
                Display::drawRow(runStart, span.y, x - 1, Graphics::rowBuffer);
                runStart = -1;
                n = 0;
            }
        }
    }
}

void RingIndicator::hide() {
    if(!_hidden) {
        // Redraw the area of the current arc (and track) as background
        _hidden = true;
        float end = _radValue;
        if(_timerStyle && _trackValue > end) end = _trackValue;
        renderAngles(_radMin + _radOffset, end + _radOffset);
    }
    _radValue = _radMin;
    _trackValue = _radMin;
}

void RingIndicator::setTrack(float percValue) {
    if(!_timerStyle) return;
    float value = (clamp01(percValue / 100.0f)) * (_radMax - _radMin) + _radMin;
    if(value == _trackValue) return;

    float oldValue = _trackValue;
    _trackValue = value;
    if(_hidden) {
        // The ring becomes visible with its (so far empty) fill
        _hidden = false;
        _radValue = _radMin;
        renderAngles(_radMin + _radOffset, _trackValue + _radOffset);
    } else {
        renderAngles(oldValue + _radOffset, value + _radOffset);
    }
}

void RingIndicator::setFillVisible(bool visible) {
    if(visible == _fillVisible) return;
    _fillVisible = visible;
    if(_hidden || !_timerStyle) return;
    // Redraw the fill area including its rounded ends and the head's glow
    float pad = HEAD_GLOW_RADIUS / _radius;
    renderAngles(_radMin + _radOffset - pad, _radValue + _radOffset + pad);
}

void RingIndicator::setHead(bool head) {
    if(head == _head) return;
    _head = head;
    if(_hidden || !_timerStyle) return;
    // Redraw the area around the end of the fill
    float pad = HEAD_GLOW_RADIUS / _radius;
    float end = _radValue + _radOffset;
    renderAngles(end - pad, end + pad);
}

void RingIndicator::updatePrec(float percValue, uint16_t color) {
    // Clip to [0, 100]
    if(percValue > 100) {
        sout.warn() <= "RingIndicator::updatePrec: percValue > 100. Clipping to 100";
        percValue = 100;
    }
    if(percValue < 0) {
        sout.warn() <= "RingIndicator::updatePrec: percValue < 0. Clipping to 0";
        percValue = 0;
    }

    // Convert to rad in [_radMin _radMax] interval
    float radValue = (percValue/100)*(_radMax - _radMin) + _radMin;

    // Perform update based on radiant value
    update(radValue, color);
}

void RingIndicator::update(float radValue, uint16_t color) {
    // Clip to [_minRad, _maxRad]
    if(radValue < _radMin) {
        sout.warn() <= "RingIndicator::update: radValue < _radMin. Clipping value to _radMin.";
        radValue = _radMin;
    }
    if (radValue > _radMax) {
        sout.warn() <= "RingIndicator::update: radValue > _radMax. Clipping value to _radMax.";
        radValue = _radMax;
    }

    // In timer style the colors come from the gradient
    if(_timerStyle) color = _color;

    float oldValue = _radValue;

    if(_hidden) {
        // Ring was hidden, draw the complete arc
        _hidden = false;
        _color = color;
        _radValue = radValue;
        float end = _radValue;
        if(_timerStyle && _trackValue > end) end = _trackValue;
        renderAngles(_radMin + _radOffset, end + _radOffset);
    } else if(color != _color) {
        // Color changed: redraw everything covered by the old or the new arc
        _color = color;
        _radValue = radValue;
        float end = (oldValue > radValue) ? oldValue : radValue;
        renderAngles(_radMin + _radOffset, end + _radOffset);
    } else if(radValue != oldValue) {
        // Only the area around the moving end of the arc changes
        _radValue = radValue;
        renderAngles(oldValue + _radOffset, radValue + _radOffset);
    }
}
