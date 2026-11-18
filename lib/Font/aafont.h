#ifndef _AAFONT_H
#define _AAFONT_H

#include <stdint.h>

/**
 * Glyph of an anti-aliased font. The bitmap stores one alpha byte per pixel
 * (0 = background, 255 = fully covered), row by row.
 * xOffset / yOffset are relative to the left end of the baseline, just like in
 * the Adafruit GFX fonts.
 */
struct AAGlyph {
    uint32_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    uint8_t xAdvance;
    int8_t xOffset;
    int8_t yOffset;
};

/**
 * Anti-aliased font covering the characters first..last.
 * Characters that were not converted have a width of zero.
 */
struct AAFont {
    const uint8_t* bitmap;
    const AAGlyph* glyphs;
    // Optional draw order per pixel (same layout as bitmap), nullptr if the
    // font has none. 0 = the pen starts here, 255 = the pen ends here.
    const uint8_t* order;
    // Optional pen path per glyph (only with order): penPoints (x, y) pairs
    // relative to the glyph's inked area, glyphs in the order first..last.
    // Glyphs without ink have their path filled with zeros.
    const uint8_t* pen;
    uint8_t penPoints;
    uint8_t first;
    uint8_t last;
    uint8_t yAdvance;
};

#endif
