#include <counter.h>
#include <graphics.h>
#include <Roboto_Light50pt_aa.h>
#include <config.h>
#include <display.h>
#include <math.h>

static const AAFont& COUNTER_FONT = Roboto_Light50pt_aa;

// Soft leading edge of the drawn stroke, in draw order units (0..255)
static constexpr float PEN_SOFT_EDGE = 40.0f;
// Share of the writing animation used for writing, the rest fades the pen out
static constexpr float PEN_WRITE_END = 0.88f;
// Share of each digit's time the pen needs to travel to the digit's start
static constexpr float PEN_TRAVEL_SHARE = 0.25f;

static inline float clamp01(float t) {
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}

static inline float smoothstep(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

Counter::Counter() :
    _nDigits(0), _color(COUNTER_COLOR), _xCenter(COUNTER_CENTER_X), _yCenter(COUNTER_CENTER_Y),
    _writing(false), _writeProgress(1.0f), _penStartX(0), _penStartY(0),
    _penX(0), _penY(0), _penRadius(0), _penAlpha(0) {

    for(int i = 0; i < MAX_DIGITS; i++) _reveal[i] = 1.0f;
    // The pen and its glow may stick out of the digit area a bit
    _penMargin = (int)ceilf(PEN_DOT_RADIUS * 2.0f) + 2;

    // Find the digit cell width and the vertical extent of all digits
    int cellWidth = 0;
    int ascent = 0;
    int descent = 0;
    for(char c = '0'; c <= '9'; c++) {
        const AAGlyph* glyph = Graphics::getGlyph(COUNTER_FONT, c);
        if(glyph == nullptr) continue;
        int right = glyph->xOffset + glyph->width;
        if(glyph->xAdvance > cellWidth) cellWidth = glyph->xAdvance;
        if(right > cellWidth) cellWidth = right;
        if(-glyph->yOffset > ascent) ascent = -glyph->yOffset;
        if(glyph->yOffset + glyph->height > descent) descent = glyph->yOffset + glyph->height;
    }

    _cellWidth = cellWidth;
    // Center the digits vertically around _yCenter
    _baseline = _yCenter + (ascent - descent) / 2;
    _top = _baseline - ascent;
    _bottom = _baseline + descent - 1;
}

uint8_t Counter::getDigits(uint8_t value, uint8_t* buffer) {
    uint8_t nDigits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
    for(int i = nDigits - 1; i >= 0; i--) {
        buffer[i] = value % 10;
        value /= 10;
    }
    return nDigits;
}

int Counter::layoutLeft(uint8_t nDigits) {
    return _xCenter - (nDigits * _cellWidth) / 2;
}

void Counter::penOnDigit(uint8_t index, float r, float& x, float& y) {
    const AAGlyph* glyph = Graphics::getGlyph(COUNTER_FONT, '0' + _digits[index]);
    int cellLeft = layoutLeft(_nDigits) + index * _cellWidth;
    if(glyph == nullptr || COUNTER_FONT.pen == nullptr) {
        x = cellLeft + _cellWidth * 0.5f;
        y = _baseline - 35.0f;
        return;
    }

    // The pen sits in the middle of the soft edge of the drawn stroke
    float order = r * (255.0f + PEN_SOFT_EDGE) - PEN_SOFT_EDGE * 0.5f;
    float pos = clamp01(order / 255.0f) * (COUNTER_FONT.penPoints - 1);
    int i0 = (int)pos;
    int i1 = (i0 + 1 < COUNTER_FONT.penPoints) ? i0 + 1 : i0;
    float f = pos - i0;

    const uint8_t* path = &COUNTER_FONT.pen[(('0' + _digits[index]) - COUNTER_FONT.first) * COUNTER_FONT.penPoints * 2];
    float px = path[2 * i0] + (path[2 * i1] - path[2 * i0]) * f;
    float py = path[2 * i0 + 1] + (path[2 * i1 + 1] - path[2 * i0 + 1]) * f;

    x = cellLeft + glyph->xOffset + px + 0.5f;
    y = _baseline + glyph->yOffset + py + 0.5f;
}

void Counter::updatePen() {
    if(!_writing) {
        for(int i = 0; i < MAX_DIGITS; i++) _reveal[i] = 1.0f;
        _penAlpha = 0;
        return;
    }

    uint8_t n = _nDigits > 0 ? _nDigits : 1;
    float t = _writeProgress;

    if(t < PEN_WRITE_END) {
        // The digits are written one after another. Each digit: the pen
        // travels to its start, then draws it.
        float segment = PEN_WRITE_END / n;
        int current = (int)(t / segment);
        if(current >= n) current = n - 1;
        float local = (t - current * segment) / segment;

        for(int i = 0; i < MAX_DIGITS; i++) {
            _reveal[i] = (i < current) ? 1.0f : 0.0f;
        }

        if(local < PEN_TRAVEL_SHARE) {
            // Travel from the previous position to the digit's start
            float fromX = _penStartX, fromY = _penStartY;
            if(current > 0) penOnDigit(current - 1, 1.0f, fromX, fromY);
            float toX, toY;
            penOnDigit(current, 0.0f, toX, toY);
            float k = smoothstep(local / PEN_TRAVEL_SHARE);
            _penX = fromX + (toX - fromX) * k;
            _penY = fromY + (toY - fromY) * k;
        } else {
            float r = smoothstep((local - PEN_TRAVEL_SHARE) / (1.0f - PEN_TRAVEL_SHARE));
            _reveal[current] = r;
            penOnDigit(current, r, _penX, _penY);
        }
        _penRadius = PEN_DOT_RADIUS;
        _penAlpha = 255;
    } else {
        // All digits are written, the pen fades out where it stopped
        for(int i = 0; i < MAX_DIGITS; i++) _reveal[i] = 1.0f;
        penOnDigit(n - 1, 1.0f, _penX, _penY);
        float k = clamp01((t - PEN_WRITE_END) / (1.0f - PEN_WRITE_END));
        _penRadius = PEN_DOT_RADIUS * (1.0f - 0.5f * k);
        _penAlpha = (uint8_t)(255.0f * (1.0f - k) + 0.5f);
    }
}

void Counter::render(int x0, int x1, int y0, int y1) {
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if(y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    if(x0 > x1 || y0 > y1) return;

    int left = layoutLeft(_nDigits);
    bool hasOrder = COUNTER_FONT.order != nullptr;

    // Per digit: glyph, position of its inked area and the pen position in
    // draw order units (fixed point, all integer math in the pixel loop)
    struct Cell {
        const AAGlyph* glyph;
        const uint8_t* bitmap;
        const uint8_t* order;
        int inkLeft, inkTop;
        bool revealing;
        int32_t penPosition;    // draw order units * 16
    } cells[MAX_DIGITS];
    const int32_t softEdge16 = (int32_t)(PEN_SOFT_EDGE * 16.0f);
    for(int i = 0; i < _nDigits; i++) {
        Cell& c = cells[i];
        c.glyph = Graphics::getGlyph(COUNTER_FONT, '0' + _digits[i]);
        c.bitmap = c.glyph ? &COUNTER_FONT.bitmap[c.glyph->bitmapOffset] : nullptr;
        c.order = (c.glyph && hasOrder) ? &COUNTER_FONT.order[c.glyph->bitmapOffset] : nullptr;
        c.inkLeft = left + i * _cellWidth + (c.glyph ? c.glyph->xOffset : 0);
        c.inkTop = _baseline + (c.glyph ? c.glyph->yOffset : 0);
        c.revealing = _reveal[i] < 1.0f && c.order != nullptr;
        c.penPosition = (int32_t)(_reveal[i] * (255.0f + PEN_SOFT_EDGE) * 16.0f);
    }

    // Pen dot: solid anti-aliased core with a faint glow around it
    bool pen = _writing && _penAlpha > 0;
    const int32_t PQ = 16;
    int32_t penX = (int32_t)(_penX * PQ), penY = (int32_t)(_penY * PQ);
    int32_t penR = (int32_t)(_penRadius * PQ);
    int32_t glowR = penR * 2;
    int32_t glowR2 = glowR * glowR;
    int penY0 = (penY - glowR) / PQ - 1, penY1 = (penY + glowR) / PQ + 1;
    int penX0 = (penX - glowR) / PQ - 1, penX1 = (penX + glowR) / PQ + 1;

    for(int y = y0; y <= y1; y++) {
        int cell = -1;
        int cellEnd = left;
        for(int x = x0; x <= x1; x++) {
            // Find the digit cell of this column (cells are next to each other)
            if(x >= cellEnd || cell < 0) {
                cell = (x - left) / _cellWidth;
                if(x < left || cell >= _nDigits) cell = _nDigits;
                cellEnd = (cell < _nDigits) ? left + (cell + 1) * _cellWidth : LCD_WIDTH;
                if(x < left) cellEnd = left;
            }

            uint8_t alpha = 0;
            if(cell < _nDigits && cells[cell].glyph != nullptr) {
                const Cell& c = cells[cell];
                int gx = x - c.inkLeft;
                int gy = y - c.inkTop;
                if(gx >= 0 && gy >= 0 && gx < c.glyph->width && gy < c.glyph->height) {
                    int idx = gy * c.glyph->width + gx;
                    alpha = c.bitmap[idx];
                    if(alpha > 0 && c.revealing) {
                        // A pixel appears once the pen has passed its draw order
                        int32_t f = c.penPosition - c.order[idx] * 16;
                        if(f <= 0) alpha = 0;
                        else if(f < softEdge16) alpha = (uint8_t)((alpha * f) / softEdge16);
                    }
                }
            }
            uint16_t color = Graphics::blend(_color, alpha);

            if(pen && y >= penY0 && y <= penY1 && x >= penX0 && x <= penX1) {
                int32_t dx = x * PQ + PQ / 2 - penX;
                int32_t dy = y * PQ + PQ / 2 - penY;
                int32_t d2 = dx*dx + dy*dy;
                if(d2 < glowR2) {
                    float dist = sqrtf((float)d2);
                    float coverage = clamp01(0.5f - (dist - penR) / PQ);
                    float glow = 0.35f * (1.0f - dist / glowR);
                    float k = coverage > glow ? coverage : glow;
                    color = Graphics::mix(color, FACE_EYE_COLOR, (uint8_t)(k * _penAlpha));
                }
            }
            Graphics::rowBuffer[x - x0] = color;
        }
        Display::drawRow(x0, y, x1, Graphics::rowBuffer);
    }
}

void Counter::render(int x0, int x1) {
    render(x0, x1, _top, _bottom);
}

void Counter::startWriting(float penX, float penY) {
    _writing = true;
    _writeProgress = 0.0f;
    _penStartX = penX;
    _penStartY = penY;
    _penX = penX;
    _penY = penY;
    updatePen();
}

void Counter::setWriting(float t) {
    if(!_writing) return;
    t = clamp01(t);
    if(t == _writeProgress) return;
    _writeProgress = t;
    updatePen();

    // Redraw the digits and the area the pen can reach
    int n = _nDigits > 0 ? _nDigits : 1;
    int left = layoutLeft(n);
    render(left - _penMargin, left + n * _cellWidth - 1 + _penMargin,
           _top - _penMargin, _bottom + _penMargin);

    if(t >= 1.0f) {
        _writing = false;
        updatePen();
    }
}

void Counter::hide() {
    bool wasWriting = _writing;
    _writing = false;
    updatePen();
    if(_nDigits == 0 && !wasWriting) return;
    int n = _nDigits > 0 ? _nDigits : 1;
    int left = layoutLeft(n);
    int right = left + n * _cellWidth - 1;
    _nDigits = 0;
    if(wasWriting) {
        render(left - _penMargin, right + _penMargin, _top - _penMargin, _bottom + _penMargin);
    } else {
        render(left, right);
    }
}

void Counter::update(uint8_t value, uint16_t color) {
    uint8_t digitsNew[MAX_DIGITS];
    uint8_t nDigitsNew = getDigits(value, digitsNew);

    if(nDigitsNew != _nDigits || color != _color) {
        // Layout or color changed: redraw the area of the old and the new number
        int oldLeft = layoutLeft(_nDigits);
        int oldRight = oldLeft + _nDigits * _cellWidth - 1;
        bool hadDigits = _nDigits > 0;

        _nDigits = nDigitsNew;
        _color = color;
        for(int i = 0; i < _nDigits; i++) _digits[i] = digitsNew[i];
        if(_writing) updatePen();

        int newLeft = layoutLeft(_nDigits);
        int newRight = newLeft + _nDigits * _cellWidth - 1;
        if(hadDigits) {
            if(oldLeft < newLeft) newLeft = oldLeft;
            if(oldRight > newRight) newRight = oldRight;
        }
        if(_writing) {
            render(newLeft - _penMargin, newRight + _penMargin, _top - _penMargin, _bottom + _penMargin);
        } else {
            render(newLeft, newRight);
        }
    } else {
        // Only redraw the digits that changed
        int left = layoutLeft(_nDigits);
        for(int i = 0; i < _nDigits; i++) {
            if(_digits[i] != digitsNew[i]) {
                _digits[i] = digitsNew[i];
                if(_writing) updatePen();
                int cellLeft = left + i * _cellWidth;
                render(cellLeft, cellLeft + _cellWidth - 1);
            }
        }
    }
}
