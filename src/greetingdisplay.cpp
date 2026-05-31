#include <greetingdisplay.h>
#include <graphics.h>
#include <display.h>
#include <config.h>
#include <pico/rand.h>

static inline void drawHorizontalLine(int x0, int y, int x1, uint16_t color) {
    if(x0 > x1) return;
    for(int x = x0; x <= x1; x++) {
        Display::setPixel(x, y, color);
    }
}

static inline void drawVerticalLine(int x, int y0, int y1, uint16_t color) {
    if(y0 > y1) return;
    for(int y = y0; y <= y1; y++) {
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

static inline void fillEyeInterior(int xCenter, int yCenter, uint16_t color) {
    int left = xCenter - (64 / 2) + 1;
    int top = yCenter - (38 / 2) + 1;
    fillRect(left, top, 64 - 2, 38 - 2, color);
}

GreetingDisplay::GreetingDisplay() :
    _xCenter(COUNTER_CENTER_X),
    _yCenter(COUNTER_CENTER_Y),
    _hidden(true),
    _lastAnimationMillis(0),
    _lastBlinkMillis(0),
    _lastGazeMillis(0),
    _nextBlinkDelay(0),
    _currentGazeX(0),
    _currentGazeY(0),
    _isBlinking(false),
    _isExcited(false),
    _prevEyeClosed(false),
    _blinkStartMillis(0),
    _lastMouthExpressionMillis(0),
    _nextMouthExpressionDelay(0),
    _mouthAnimationPhase(0),
    _mouthExpression(MOUTH_EXPRESSION_SMILE),
    _color(WHITE) {
}

void GreetingDisplay::chooseNewGaze() {
    if(_isExcited) {
        switch(get_rand_32() % 4) {
            case 0:
                _currentGazeX = 0;
                _currentGazeY = -2;
                break;
            case 1:
                _currentGazeX = -MAX_GAZE_OFFSET_X;
                _currentGazeY = -1;
                break;
            case 2:
                _currentGazeX = MAX_GAZE_OFFSET_X;
                _currentGazeY = -1;
                break;
            default:
                _currentGazeX = 0;
                _currentGazeY = 0;
                break;
        }
    } else {
        switch(get_rand_32() % 4) {
            case 0:
                _currentGazeX = 0;
                _currentGazeY = 0;
                break;
            case 1:
                _currentGazeX = -MAX_GAZE_OFFSET_X;
                _currentGazeY = -MAX_GAZE_OFFSET_Y;
                break;
            case 2:
                _currentGazeX = MAX_GAZE_OFFSET_X;
                _currentGazeY = -MAX_GAZE_OFFSET_Y;
                break;
            default:
                _currentGazeX = 0;
                _currentGazeY = MAX_GAZE_OFFSET_Y;
                break;
        }
    }
    _lastGazeMillis = millis();
}

void GreetingDisplay::clearEyeArea() {
    int x0 = _xCenter - ((EYE_POD_WIDTH * 2 + EYE_GAP) / 2) - EYE_AREA_PADDING;
    int y0 = _yCenter - (EYE_POD_HEIGHT / 2) - EYE_AREA_PADDING;
    int width = EYE_POD_WIDTH * 2 + EYE_GAP + (EYE_AREA_PADDING * 2);
    int height = EYE_POD_HEIGHT + (EYE_AREA_PADDING * 2) + MOUTH_OFFSET_Y + 8;
    fillRect(x0, y0, width, height, BLACK);
}

void GreetingDisplay::eraseEyeInterior(uint8_t xCenter, uint8_t yCenter) {
    fillEyeInterior(xCenter, yCenter, WHITE);
}

void GreetingDisplay::erasePreviousPupil(uint8_t xCenter, uint8_t yCenter, int8_t prevOffsetX, int8_t prevOffsetY) {
    int pupilX = xCenter + prevOffsetX;
    int pupilY = yCenter + prevOffsetY;
    int margin = EYE_PUPIL_RADIUS + 6;
    fillRect(pupilX - margin, pupilY - margin, margin * 2 + 1, margin * 2 + 1, WHITE);
}

void GreetingDisplay::drawEyePod(uint8_t xCenter, uint8_t yCenter, uint16_t color) {
    fillEyeInterior(xCenter, yCenter, WHITE);
    int left = xCenter - (EYE_POD_WIDTH / 2);
    int top = yCenter - (EYE_POD_HEIGHT / 2);
    int right = left + EYE_POD_WIDTH - 1;
    int bottom = top + EYE_POD_HEIGHT - 1;
    int corner = EYE_CORNER_RADIUS;

    drawHorizontalLine(left + corner, top, right - corner, color);
    drawHorizontalLine(left + corner, bottom, right - corner, color);
    drawVerticalLine(left, top + corner, bottom - corner, color);
    drawVerticalLine(right, top + corner, bottom - corner, color);

    Graphics::drawFullCircle(left + corner, top + corner, corner, color);
    Graphics::drawFullCircle(right - corner, top + corner, corner, color);
    Graphics::drawFullCircle(left + corner, bottom - corner, corner, color);
    Graphics::drawFullCircle(right - corner, bottom - corner, corner, color);
}

void GreetingDisplay::drawBlink(uint8_t xCenter, uint8_t yCenter, uint16_t color) {
    int left = xCenter - (EYE_POD_WIDTH / 2) + 8;
    int right = xCenter + (EYE_POD_WIDTH / 2) - 8;
    int y = yCenter;
    for(int offset = -1; offset <= 1; offset++) {
        drawHorizontalLine(left, y + offset, right, color);
    }
}

void GreetingDisplay::drawPupil(uint8_t xCenter, uint8_t yCenter, int8_t offsetX, int8_t offsetY, uint16_t color) {
    int pupilX = xCenter + offsetX;
    int pupilY = yCenter + offsetY;

    Graphics::drawFullCircle(pupilX, pupilY, EYE_PUPIL_RADIUS, BLACK);
    Graphics::drawFullCircle(pupilX + 2, pupilY - 2, 2, WHITE);
    Graphics::drawFullCircle(pupilX + 5, pupilY - 5, 1, WHITE);
}

void GreetingDisplay::drawMouth(uint8_t xCenter, uint8_t yCenter, uint16_t color) {
    int left = xCenter - MOUTH_HALF_WIDTH;
    int right = xCenter + MOUTH_HALF_WIDTH;

    switch(_mouthExpression) {
        case MOUTH_EXPRESSION_BOW:
            drawHorizontalLine(left + 3, yCenter + 1, right - 3, color);
            Display::setPixel(left + 2, yCenter, color);
            Display::setPixel(right - 2, yCenter, color);
            break;

        case MOUTH_EXPRESSION_DOT:
            Display::setPixel(xCenter, yCenter, color);
            break;

        default:
            if(_mouthAnimationPhase == 0) {
                Display::setPixel(left + 1, yCenter, color);
                Display::setPixel(right - 2, yCenter, color);
                Display::setPixel(left + 2, yCenter + 1, color);
                Display::setPixel(right - 3, yCenter + 1, color);
                drawHorizontalLine(left + 3, yCenter + 2, right - 4, color);
                drawHorizontalLine(left + 4, yCenter + 3, right - 5, color);
                drawHorizontalLine(left + 5, yCenter + 4, right - 6, color);
            } else if(_mouthAnimationPhase == 1) {
                Display::setPixel(left + 1, yCenter + 1, color);
                Display::setPixel(right - 2, yCenter + 1, color);
                Display::setPixel(left + 2, yCenter + 2, color);
                Display::setPixel(right - 3, yCenter + 2, color);
                drawHorizontalLine(left + 3, yCenter + 3, right - 4, color);
                drawHorizontalLine(left + 4, yCenter + 4, right - 5, color);
                drawHorizontalLine(left + 5, yCenter + 5, right - 6, color);
            } else if(_mouthAnimationPhase == 2) {
                Display::setPixel(left + 1, yCenter + 2, color);
                Display::setPixel(right - 2, yCenter + 2, color);
                Display::setPixel(left + 2, yCenter + 3, color);
                Display::setPixel(right - 3, yCenter + 3, color);
                drawHorizontalLine(left + 3, yCenter + 4, right - 4, color);
                drawHorizontalLine(left + 4, yCenter + 5, right - 5, color);
                drawHorizontalLine(left + 5, yCenter + 6, right - 6, color);
            } else if(_mouthAnimationPhase == 3) {
                Display::setPixel(left + 1, yCenter + 1, color);
                Display::setPixel(right - 2, yCenter + 1, color);
                Display::setPixel(left + 2, yCenter + 2, color);
                Display::setPixel(right - 3, yCenter + 2, color);
                drawHorizontalLine(left + 3, yCenter + 3, right - 4, color);
            } else if(_mouthAnimationPhase == 4) {
                Display::setPixel(left + 1, yCenter, color);
                Display::setPixel(right - 2, yCenter, color);
                Display::setPixel(left + 2, yCenter + 2, color);
                Display::setPixel(right - 3, yCenter + 2, color);
                drawHorizontalLine(left + 3, yCenter + 3, right - 4, color);
                drawHorizontalLine(left + 4, yCenter + 4, right - 5, color);
            } else {
                Display::setPixel(left + 1, yCenter + 2, color);
                Display::setPixel(right - 2, yCenter + 2, color);
                Display::setPixel(left + 2, yCenter + 3, color);
                Display::setPixel(right - 3, yCenter + 3, color);
                drawHorizontalLine(left + 3, yCenter + 4, right - 4, color);
            }
            break;
    }
}

void GreetingDisplay::chooseNewMouthExpression() {
    uint32_t choice = get_rand_32() % 10;
    if(choice < 5) {
        _mouthExpression = MOUTH_EXPRESSION_SMILE;
    } else if(choice < 8) {
        _mouthExpression = MOUTH_EXPRESSION_BOW;
    } else {
        _mouthExpression = MOUTH_EXPRESSION_DOT;
    }
}

void GreetingDisplay::stepAnimation() {
    uint32_t now = millis();
    if(now - _lastAnimationMillis < ANIMATION_INTERVAL_MS) {
        return;
    }
    _lastAnimationMillis = now;

    bool pupilStateChanged = false;
    bool mouthStateChanged = false;
    if(now - _lastMouthAnimationMillis >= MOUTH_ANIMATION_INTERVAL_MS) {
        _lastMouthAnimationMillis = now;
        _mouthAnimationPhase = (_mouthAnimationPhase + 1) % MOUTH_ANIMATION_STEP_COUNT;
        mouthStateChanged = true;
    }

    if(_nextMouthExpressionDelay == 0 || now - _lastMouthExpressionMillis >= _nextMouthExpressionDelay) {
        chooseNewMouthExpression();
        _lastMouthExpressionMillis = now;
        _nextMouthExpressionDelay = MOUTH_EXPRESSION_INTERVAL_MIN_MS + (get_rand_32() % (MOUTH_EXPRESSION_INTERVAL_MAX_MS - MOUTH_EXPRESSION_INTERVAL_MIN_MS + 1));
        mouthStateChanged = true;
    }

    bool currentlyClosed = _isBlinking;

    if(_isBlinking) {
        if(now - _blinkStartMillis >= BLINK_DURATION_MS) {
            _isBlinking = false;
            _nextBlinkDelay = BLINK_INTERVAL_MIN_MS + (get_rand_32() % (BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS + 1));
            _lastBlinkMillis = now;
            chooseNewGaze();
            pupilStateChanged = true;
        }
    } else {
        if(_nextBlinkDelay == 0) {
            _nextBlinkDelay = BLINK_INTERVAL_MIN_MS + (get_rand_32() % (BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS + 1));
            _lastBlinkMillis = now;
        } else if(now - _lastBlinkMillis >= _nextBlinkDelay) {
            _isBlinking = true;
            _blinkStartMillis = now;
            pupilStateChanged = true;
        }

        if(now - _lastGazeMillis >= FRAME_HOLD_MS) {
            chooseNewGaze();
            pupilStateChanged = true;
        }
    }

    if(!mouthStateChanged && !pupilStateChanged &&
       !currentlyClosed && _currentGazeX == _prevGazeX && _currentGazeY == _prevGazeY) {
        return;
    }

    uint8_t leftEyeX = _xCenter - (EYE_POD_WIDTH / 2) - (EYE_GAP / 2);
    uint8_t rightEyeX = _xCenter + (EYE_POD_WIDTH / 2) + (EYE_GAP / 2);

    if(pupilStateChanged) {
        if(_prevEyeClosed) {
            eraseEyeInterior(leftEyeX, _yCenter);
            eraseEyeInterior(rightEyeX, _yCenter);
        } else {
            erasePreviousPupil(leftEyeX, _yCenter, _prevGazeX, _prevGazeY);
            erasePreviousPupil(rightEyeX, _yCenter, _prevGazeX, _prevGazeY);
        }

        if(_isBlinking) {
            drawBlink(leftEyeX, _yCenter, _color);
            drawBlink(rightEyeX, _yCenter, _color);
        } else {
            drawPupil(leftEyeX, _yCenter, _currentGazeX, _currentGazeY, _color);
            drawPupil(rightEyeX, _yCenter, _currentGazeX, _currentGazeY, _color);
        }
    }

    int mouthYOffset = 0;
    switch(_mouthAnimationPhase) {
        case 1:
            mouthYOffset = 1;
            break;
        case 2:
            mouthYOffset = 2;
            break;
        case 3:
            mouthYOffset = 1;
            break;
        default:
            mouthYOffset = 0;
            break;
    }

    if(mouthStateChanged) {
        drawMouth(_xCenter, _yCenter + (EYE_POD_HEIGHT / 2) + MOUTH_OFFSET_Y + mouthYOffset, _color);
    }

    if(pupilStateChanged) {
        _prevGazeX = _currentGazeX;
        _prevGazeY = _currentGazeY;
        _prevEyeClosed = currentlyClosed;
    }
}

void GreetingDisplay::showRandom(uint16_t color) {
    if(!_hidden) {
        hide();
    }

    _color = color;
    _hidden = false;
    _currentGazeX = 0;
    _currentGazeY = 0;
    _prevGazeX = 0;
    _prevGazeY = 0;
    _prevEyeClosed = false;
    _isExcited = true;
    _nextBlinkDelay = 0;
    _isBlinking = false;
    _blinkStartMillis = 0;
    _lastAnimationMillis = 0;
    _lastMouthAnimationMillis = millis();
    _nextMouthExpressionDelay = MOUTH_EXPRESSION_INTERVAL_MIN_MS + (get_rand_32() % (MOUTH_EXPRESSION_INTERVAL_MAX_MS - MOUTH_EXPRESSION_INTERVAL_MIN_MS + 1));
    _mouthAnimationPhase = 0;
    _lastGazeMillis = 0;

    uint8_t leftEyeX = _xCenter - (EYE_POD_WIDTH / 2) - (EYE_GAP / 2);
    uint8_t rightEyeX = _xCenter + (EYE_POD_WIDTH / 2) + (EYE_GAP / 2);
    drawEyePod(leftEyeX, _yCenter, _color);
    drawEyePod(rightEyeX, _yCenter, _color);
    chooseNewGaze();
    drawPupil(leftEyeX, _yCenter, _currentGazeX, _currentGazeY, _color);
    drawPupil(rightEyeX, _yCenter, _currentGazeX, _currentGazeY, _color);
    drawMouth(_xCenter, _yCenter + (EYE_POD_HEIGHT / 2) + MOUTH_OFFSET_Y, _color);
    _prevGazeX = _currentGazeX;
    _prevGazeY = _currentGazeY;
}

void GreetingDisplay::hide() {
    if(_hidden) return;
    clearEyeArea();
    _hidden = true;
}

void GreetingDisplay::animateEyes() {
    if(_hidden) {
        return;
    }
    stepAnimation();
}

