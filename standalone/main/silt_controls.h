// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stdint.h>
void silt_controls_init(void);
void silt_controls_poll(float delta);
bool silt_controls_active(void);
void silt_draw_band(uint16_t *dst, int y0, int rows);
void silt_boot_message(const char *text);
