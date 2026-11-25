// SPDX-License-Identifier: MIT
#include "bsp_pca9557.h"

#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "pca9557";

#define REG_INPUT_PORT       0x00
#define REG_OUTPUT_PORT      0x01
#define REG_POLARITY_INV     0x02
#define REG_CONFIG           0x03

static uint8_t s_out_shadow;
static bool s_inited;

esp_err_t bsp_pca9557_write(uint8_t value)
{
    esp_err_t err = bsp_i2c_write_reg(BSP_PCA9557_ADDR, REG_OUTPUT_PORT, value);
    if (err == ESP_OK) {
        s_out_shadow = value;
    }
    return err;
}

esp_err_t bsp_pca9557_set(uint8_t bit, bool level)
{
    uint8_t mask = (uint8_t)(1u << bit);
    uint8_t value = level ? (s_out_shadow | mask) : (s_out_shadow & (uint8_t)~mask);
    return bsp_pca9557_write(value);
}

uint8_t bsp_pca9557_output(void)
{
    return s_out_shadow;
}

esp_err_t bsp_pca9557_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    /* Output latch first (official order): LCD_CS=1 (idle), PA_EN=0, DVP_PWDN=1. */
    esp_err_t err = bsp_pca9557_write(0x05);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "output write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Only bits 0..2 become outputs (config bit 1 = input), rest stay inputs. */
    err = bsp_i2c_write_reg(BSP_PCA9557_ADDR, REG_CONFIG, 0xF8);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* No polarity inversion. */
    err = bsp_i2c_write_reg(BSP_PCA9557_ADDR, REG_POLARITY_INV, 0x00);
    if (err != ESP_OK) {
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "ready, OUT=0x%02X (LCD_CS=1 idle, PA_EN=0, DVP_PWDN=1)", s_out_shadow);
    return ESP_OK;
}
