// SPDX-License-Identifier: MIT
/*
 * PCA9557 I2C IO expander.
 * Used for LCD_CS (bit0), speaker PA enable (bit1), camera power-down (bit2).
 * The board cannot light up the LCD until this chip is configured.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure all three used pins as outputs and apply power-on defaults:
 *   LCD_CS   = 0  (selected, stays low forever - single SPI device)
 *   PA_EN    = 0  (amplifier off until audio starts, avoids pop)
 *   DVP_PWDN = 1  (camera powered down, unused)
 */
esp_err_t bsp_pca9557_init(void);

/* Set/clear individual output bits (bit index from bsp_pins.h). */
esp_err_t bsp_pca9557_set(uint8_t bit, bool level);

/* Write the whole output port (shadow register kept in sync). */
esp_err_t bsp_pca9557_write(uint8_t value);

uint8_t bsp_pca9557_output(void);

#ifdef __cplusplus
}
#endif
