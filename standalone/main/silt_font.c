// SPDX-License-Identifier: GPL-3.0-only
#include "silt_font.h"

#include <limits.h>
#include <stddef.h>

enum { SCREEN_WIDTH = 320, ASCII_SIZE = 8, CJK_SIZE = 14, CJK_ADVANCE = 16 };

/* Public-domain table defined by silt_controls.c. */
extern char font8x8_basic[128][8];

struct SiltGlyph {
    uint16_t code;
    uint16_t rows[CJK_SIZE];
};

#include "../assets/silt_font_data.inc"

static uint32_t next_codepoint(const char **text)
{
    const unsigned char *bytes = (const unsigned char *)*text;
    uint32_t lead = *bytes++;
    *text = (const char *)bytes;
    if (lead < 0x80) return lead;

    uint32_t value;
    uint32_t minimum;
    unsigned count;
    if (lead >= 0xC2 && lead <= 0xDF) {
        value = lead & 0x1F;
        minimum = 0x80;
        count = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        value = lead & 0x0F;
        minimum = 0x800;
        count = 2;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        value = lead & 0x07;
        minimum = 0x10000;
        count = 3;
    } else {
        return '?';
    }

    for (unsigned i = 0; i < count; i++) {
        uint32_t byte = *bytes++;
        if ((byte & 0xC0) != 0x80) return '?';
        value = (value << 6) | (byte & 0x3F);
    }
    *text = (const char *)bytes;
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) return '?';
    return value;
}

static const struct SiltGlyph *find_glyph(uint32_t code)
{
    size_t lo = 0;
    size_t hi = sizeof(silt_glyphs) / sizeof(silt_glyphs[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t match = silt_glyphs[mid].code;
        if (code == match) return &silt_glyphs[mid];
        if (code < match) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

int silt_text_width(const char *text, int scale)
{
    if (!text || scale <= 0) return 0;

    int64_t total = 0;
    while (*text) {
        uint32_t code = next_codepoint(&text);
        total += (int64_t)(code < 0x80 ? ASCII_SIZE : CJK_ADVANCE) * scale;
        if (total > INT_MAX) return INT_MAX;
    }
    return (int)total;
}

static void paint_row(uint16_t *dst, int y0, int rows, int64_t x, int64_t y,
                      uint16_t mask, int width, uint16_t color, int scale)
{
    int64_t first = y > y0 ? y : y0;
    int64_t last = y + scale < (int64_t)y0 + rows ? y + scale : (int64_t)y0 + rows;
    if (first >= last) return;

    for (int column = 0; column < width; column++, x += scale, mask >>= 1) {
        if (!(mask & 1u)) continue;
        int64_t left = x > 0 ? x : 0;
        int64_t right = x + scale < SCREEN_WIDTH ? x + scale : SCREEN_WIDTH;
        if (left >= right) continue;

        for (int64_t line = first; line < last; line++) {
            uint16_t *pixels = dst + (size_t)(line - y0) * SCREEN_WIDTH;
            for (int64_t px = left; px < right; px++) pixels[px] = color;
        }
    }
}

void silt_text_draw(uint16_t *dst, int y0, int rows, int x, int y,
                    const char *text, uint16_t color, int scale)
{
    if (!dst || !text || rows <= 0 || scale <= 0) return;
    if ((int64_t)y + SILT_TEXT_LINE_HEIGHT * (int64_t)scale <= y0 ||
        y >= (int64_t)y0 + rows) return;

    static const uint16_t missing[CJK_SIZE] = {
        0, 0, 0x01F0, 0x0210, 0x0100, 0x0080, 0x0040,
        0x0040, 0, 0x0040, 0, 0, 0, 0,
    };
    int64_t cursor = x;
    while (*text && cursor < SCREEN_WIDTH) {
        uint32_t code = next_codepoint(&text);
        int advance = code < 0x80 ? ASCII_SIZE : CJK_ADVANCE;
        int64_t end = cursor + (int64_t)advance * scale;
        if (end > 0) {
            if (code < 0x80) {
                int64_t glyph_y = (int64_t)y + ((CJK_SIZE - ASCII_SIZE) / 2) * (int64_t)scale;
                for (int row = 0; row < ASCII_SIZE; row++) {
                    paint_row(dst, y0, rows, cursor, glyph_y, (uint8_t)font8x8_basic[code][row],
                              ASCII_SIZE, color, scale);
                    glyph_y += scale;
                }
            } else {
                const struct SiltGlyph *glyph = find_glyph(code);
                const uint16_t *bitmap = glyph ? glyph->rows : missing;
                int64_t glyph_y = y;
                for (int row = 0; row < CJK_SIZE; row++) {
                    paint_row(dst, y0, rows, cursor + scale, glyph_y, bitmap[row],
                              CJK_SIZE, color, scale);
                    glyph_y += scale;
                }
            }
        }
        cursor = end;
    }
}
