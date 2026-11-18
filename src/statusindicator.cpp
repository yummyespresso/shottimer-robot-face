#include <statusindicator.h>
#include <display.h>
#include <graphics.h>
#include <serialutils.h>
#include <Roboto_Light20pt_aa.h>
#include <config.h>


StatusIndicator::StatusIndicator() :
    _xCenter(STATUS_CENTER_X), _yCenter(STATUS_CENTER_Y) {

    _hidden = true;
    _color = STATUS_COLOR;

};

void StatusIndicator::drawSymbol(uint16_t color) {
    char symbol;
    switch (_state) {
        case StatusIndicatorState::TIMER:
            // 's' signals seconds
            symbol = 's';
            break;
        case StatusIndicatorState::LEVEL:
            // '%' signals percentage
            symbol = '%';
            break;
        case StatusIndicatorState::CLEAN:
            // '!' signals a cleaning hint
            symbol = '!';
            break;
        case StatusIndicatorState::ERROR:
            // '?' signals an error
            symbol = '?';
            break;
        default:
            sout.err() << "StatusIndicator::drawSymbol encountered unknown state " <= _state;
            return;
    }

    const AAGlyph* glyph = Graphics::getGlyph(Roboto_Light20pt_aa, symbol);
    if(glyph == nullptr) return;

    // Center the symbol horizontally, _yCenter is the baseline
    Graphics::drawGlyph(Roboto_Light20pt_aa, symbol,
        _xCenter - glyph->width / 2, _yCenter + glyph->yOffset, color);
}

void StatusIndicator::hide() {
    // Nothing to do if hidden already
    if(_hidden) return;

    // Draw current status symbol in black to remove it
    drawSymbol(BLACK);
    _hidden = true;
}

void StatusIndicator::showStatus(StatusIndicatorState newStatus, uint16_t color) {
    // Nothing to do if the same symbol is already shown
    if(!_hidden && newStatus == _state && color == _color) return;

    // Same symbol in a new color (e.g. while fading): draw over it directly,
    // every pixel of the symbol is rewritten, so no black frame is needed
    if(!_hidden && newStatus == _state) {
        _color = color;
        drawSymbol(color);
        return;
    }

    // Remove old symbol if not hidden
    if(!_hidden) hide();

    // Update state and draw new symbol
    _state = newStatus;
    _color = color;
    drawSymbol(color);
    _hidden = false;
}
