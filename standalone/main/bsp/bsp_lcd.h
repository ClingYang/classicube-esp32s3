// SPDX-License-Identifier: MIT
/*
 * ST7789 320x240 LCD on SPI3, 80MHz, RGB565.
 * Chip-select is PCA9557 bit0 and is held low permanently.
 * Backlight (GPIO42) is active low and dimmed with LEDC PWM.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous PSRAM-safe transfer through the internal DMA staging buffer. */
esp_err_t bsp_lcd_blit_staged(int x_start,int y_start,int x_end,int y_end,const uint16_t *data);

#define BSP_LCD_BAND_LINES 40   /* rows per transfer band */

/* Full bring-up: backlight off -> SPI bus -> panel -> init -> backlight on. */
esp_err_t bsp_lcd_init(void);

esp_lcd_panel_handle_t bsp_lcd_panel(void);

/* 0 = off, 100 = full brightness. */
esp_err_t bsp_lcd_set_backlight(int percent);

/* Blocking helpers (used by self-test and the ROM menu). */
void bsp_lcd_fill(uint16_t color);
void bsp_lcd_draw_rect(int x, int y, int w, int h, uint16_t color);

/*
 * Blit a rectangle of RGB565 pixels. x_end / y_end are EXCLUSIVE.
 * Blocking: returns after the transfer completed.
 */
esp_err_t bsp_lcd_blit(int x_start, int y_start, int x_end, int y_end, const uint16_t *data);

/* Non-blocking variant: queue the transfer, do not wait for completion. */
esp_err_t bsp_lcd_blit_async(int x_start, int y_start, int x_end, int y_end, const uint16_t *data);

/* Wait for one queued transfer to complete. Returns false on timeout. */
bool bsp_lcd_wait_transfer(uint32_t timeout_ms);

/* Drain the completion semaphore (call before starting a timing run). */
void bsp_lcd_flush_pending(void);

/* P0 self-test pattern: 8 vertical colour bars. */
void bsp_lcd_color_bars(void);

#ifdef __cplusplus
}
#endif
