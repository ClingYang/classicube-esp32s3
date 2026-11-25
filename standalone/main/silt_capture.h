/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SILT_CAPTURE_H
#define SILT_CAPTURE_H

#include <stdint.h>

/* Call once for each composed band before sending it to the display. */
void silt_capture_band(const uint16_t *pixels, int y0, int rows);

#endif
