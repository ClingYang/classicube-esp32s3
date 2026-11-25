// SPDX-License-Identifier: MIT
#include "bsp_touch.h"

#include "bsp_i2c.h"
#include "bsp_lcd.h"
#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "bsp_touch";

#define FT6336_ADDR        0x38
#define FT6336_REG_TD_STATUS 0x02   /* [3:0] = number of touch points */
#define FT6336_REG_P1_XH   0x03
#define FT6336_REG_P1_XL   0x04
#define FT6336_REG_P1_YH   0x05
#define FT6336_REG_P1_YL   0x06

static bool s_inited;

esp_err_t bsp_touch_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    if (bsp_i2c_dev(FT6336_ADDR, 0) == NULL) {
        ESP_LOGE(TAG, "cannot attach touch controller on I2C");
        return ESP_FAIL;
    }
    s_inited = true;
    ESP_LOGI(TAG, "FT6336 ready at 0x%02X", FT6336_ADDR);
    return ESP_OK;
}

bool bsp_touch_poll(bsp_touch_raw_t *out)
{
    if (!s_inited || !out) {
        return false;
    }

    uint8_t reg = FT6336_REG_TD_STATUS;
    uint8_t buf[5] = { 0 };

    if (bsp_i2c_write_read(FT6336_ADDR, &reg, 1, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    uint8_t points = buf[0] & 0x0F;
    if (points == 0) {
        out->pressed = false;
        return false;
    }

    out->pressed = true;
    out->raw_x = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    out->raw_y = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    return true;
}

/*
 * Matches the official LCSC example (esp_bsp_touch_config):
 *   .x_max = BSP_LCD_V_RES (240), .y_max = BSP_LCD_H_RES (320)
 *   .flags = { .swap_xy = 1, .mirror_x = 1, .mirror_y = 0 }
 * i.e. mirror raw_x across the 240-wide axis first, then swap the axes.
 *   screen_x = raw_y
 *   screen_y = (BSP_LCD_V_RES - 1) - raw_x
 */
void bsp_touch_map(const bsp_touch_raw_t *raw, int *x, int *y)
{
    int sx = raw->raw_y;
    int sy = (BSP_LCD_V_RES - 1) - raw->raw_x;

    if (sx < 0) sx = 0;
    if (sx >= BSP_LCD_H_RES) sx = BSP_LCD_H_RES - 1;
    if (sy < 0) sy = 0;
    if (sy >= BSP_LCD_V_RES) sy = BSP_LCD_V_RES - 1;

    *x = sx;
    *y = sy;
}
