// SPDX-License-Identifier: GPL-3.0-only
#ifndef SILT_FONT_H
#define SILT_FONT_H

#include <stdint.h>

enum { SILT_TEXT_LINE_HEIGHT = 14 };

/* Single-line UTF-8 text with CJK and ASCII advances of 16 and 8 pixels. */
int silt_text_width(const char *text, int scale);

/* Draws to a native 320-pixel-wide RGB565 band, with y0 the first display row. */
void silt_text_draw(uint16_t *dst, int y0, int rows, int x, int y,
                    const char *text, uint16_t color, int scale);

#endif
