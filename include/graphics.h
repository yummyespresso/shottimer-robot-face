#ifndef _graphics_h
#define _graphics_h

#include <pico/stdlib.h>
#include <aafont.h>

/**
 *
 * Graphics namespace includes anti-aliased drawing utilities.
 * All drawing assumes a black background: partially covered pixels are
 * blended between the given color and black.
 *
 */
namespace Graphics {
    /**
     * Blends color with the black background. alpha = 0 gives black,
     * alpha = 255 gives the unchanged color.
     */
    uint16_t blend(uint16_t color, uint8_t alpha);

    /**
     * Mixes two colors: alpha = 0 gives a, alpha = 255 gives b.
     */
    uint16_t mix(uint16_t a, uint16_t b, uint8_t alpha);

    /**
     * Fills the rectangle [x0, x1] x [y0, y1] (inclusive) with the given color.
     * Coordinates outside the display are clipped.
     */
    void fillRect(int x0, int y0, int x1, int y1, uint16_t color);

    /**
     * Returns the glyph of character c or nullptr if the font does not contain it.
     */
    const AAGlyph* getGlyph(const AAFont& font, char c);

    /**
     * Returns the alpha value of the glyph at (gx, gy), relative to the top left
     * corner of the glyph's inked area. Returns 0 outside of the glyph.
     */
    uint8_t glyphAlpha(const AAFont& font, const AAGlyph* glyph, int gx, int gy);

    /**
     * Returns the draw order of the glyph at (gx, gy) (0 = pen starts here,
     * 255 = pen ends here). Returns 0 if the font has no draw order.
     */
    uint8_t glyphOrder(const AAFont& font, const AAGlyph* glyph, int gx, int gy);

    /**
     * Draws the anti-aliased character c with the top left corner of its inked
     * area at (inkLeft, inkTop). Every pixel of the inked rectangle is written,
     * so drawing in BLACK removes the character again.
     */
    void drawGlyph(const AAFont& font, char c, int inkLeft, int inkTop, uint16_t color);

    /**
     * Shared row buffer for drawing code that writes whole rows via Display::drawRow.
     */
    extern uint16_t rowBuffer[];
}

#endif
