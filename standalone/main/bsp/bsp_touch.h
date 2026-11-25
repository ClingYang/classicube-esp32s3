// SPDX-License-Identifier: MIT
/*
 * FT6336 capacitive touch (I2C 0x38).
 * Reports raw panel coordinates; the panel is mounted 240x320 portrait while we
 * drive it as 320x240 landscape, so mapping happens in bsp_touch_map().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pressed;
    uint16_t raw_x;     /* as reported by the controller */
    uint16_t raw_y;
} bsp_touch_raw_t;

esp_err_t bsp_touch_init(void);

/* Poll the controller. Returns true when a finger is down. */
bool bsp_touch_poll(bsp_touch_raw_t *out);

/*
 * Convert raw controller coordinates to the 320x240 landscape frame buffer,
 * applying the same orientation as the display (swap_xy + mirror).
 */
void bsp_touch_map(const bsp_touch_raw_t *raw, int *x, int *y);

#ifdef __cplusplus
}
#endif
