#include <graphics.h>
#include <display.h>

namespace Graphics {

    uint16_t rowBuffer[LCD_WIDTH];

    // Both functions use shifts instead of divisions: alpha (0..255) is
    // mapped to 0..256 so that 255 keeps the color unchanged.
    uint16_t blend(uint16_t color, uint8_t alpha) {
        if(alpha == 255) return color;
        if(alpha == 0) return BLACK;
        uint32_t s = alpha + (alpha >> 7);
        uint32_t r = (((color >> 11) & 0x1F) * s + 128) >> 8;
        uint32_t g = (((color >> 5) & 0x3F) * s + 128) >> 8;
        uint32_t b = ((color & 0x1F) * s + 128) >> 8;
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    uint16_t mix(uint16_t a, uint16_t b, uint8_t alpha) {
        if(alpha == 0) return a;
        if(alpha == 255) return b;
        if(a == b) return a;
        uint32_t s = alpha + (alpha >> 7);
        uint32_t inv = 256 - s;
        uint32_t r = (((a >> 11) & 0x1F) * inv + ((b >> 11) & 0x1F) * s + 128) >> 8;
        uint32_t g = (((a >> 5) & 0x3F) * inv + ((b >> 5) & 0x3F) * s + 128) >> 8;
        uint32_t bl = ((a & 0x1F) * inv + (b & 0x1F) * s + 128) >> 8;
        return (uint16_t)((r << 11) | (g << 5) | bl);
    }

    void fillRect(int x0, int y0, int x1, int y1, uint16_t color) {
        if(x0 < 0) x0 = 0;
        if(y0 < 0) y0 = 0;
        if(x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
        if(y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
        if(x0 > x1 || y0 > y1) return;

        for(int x = x0; x <= x1; x++) rowBuffer[x - x0] = color;
        for(int y = y0; y <= y1; y++) {
            Display::drawRow(x0, y, x1, rowBuffer);
        }
    }

    const AAGlyph* getGlyph(const AAFont& font, char c) {
        uint8_t code = (uint8_t)c;
        if(code < font.first || code > font.last) return nullptr;
        const AAGlyph* glyph = &font.glyphs[code - font.first];
        if(glyph->width == 0) return nullptr;
        return glyph;
    }

    uint8_t glyphAlpha(const AAFont& font, const AAGlyph* glyph, int gx, int gy) {
        if(glyph == nullptr) return 0;
        if(gx < 0 || gy < 0 || gx >= glyph->width || gy >= glyph->height) return 0;
        return font.bitmap[glyph->bitmapOffset + gy * glyph->width + gx];
    }

    uint8_t glyphOrder(const AAFont& font, const AAGlyph* glyph, int gx, int gy) {
        if(font.order == nullptr || glyph == nullptr) return 0;
        if(gx < 0 || gy < 0 || gx >= glyph->width || gy >= glyph->height) return 0;
        return font.order[glyph->bitmapOffset + gy * glyph->width + gx];
    }

    void drawGlyph(const AAFont& font, char c, int inkLeft, int inkTop, uint16_t color) {
        const AAGlyph* glyph = getGlyph(font, c);
        if(glyph == nullptr) return;

        int x0 = inkLeft < 0 ? 0 : inkLeft;
        int x1 = inkLeft + glyph->width - 1;
        if(x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
        if(x0 > x1) return;

        for(int gy = 0; gy < glyph->height; gy++) {
            int y = inkTop + gy;
            if(y < 0 || y >= LCD_HEIGHT) continue;
            const uint8_t* row = &font.bitmap[glyph->bitmapOffset + gy * glyph->width];
            for(int x = x0; x <= x1; x++) {
                rowBuffer[x - x0] = blend(color, row[x - inkLeft]);
            }
            Display::drawRow(x0, y, x1, rowBuffer);
        }
    }

}
