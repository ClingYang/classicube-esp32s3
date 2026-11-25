// SPDX-License-Identifier: MIT
/*
 * ESP_NES - board pin map for LCSC "ShiZhanPai" ESP32-S3 (N16R8)
 *
 * Source of truth: official example header `esp32_s3_szp.h`
 * (maker-community/lcsc-shizhanpi-esp32s3-examples).
 * Do not change these numbers without checking the schematic.
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- I2C0: shared bus (PCA9557 / QMI8658 / FT6336 / ES8311 / ES7210) ---------- */
#define BSP_I2C_SDA             GPIO_NUM_1
#define BSP_I2C_SCL             GPIO_NUM_2
#define BSP_I2C_PORT_NUM        0
#define BSP_I2C_FREQ_HZ         100000

/* ---------- PCA9557 IO expander ---------- */
#define BSP_PCA9557_ADDR        0x19
#define BSP_PCA_BIT_LCD_CS      0       /* BIT(0), active low  */
#define BSP_PCA_BIT_PA_EN       1       /* BIT(1), speaker amp */
#define BSP_PCA_BIT_DVP_PWDN    2       /* BIT(2), camera power-down */

/* ---------- LCD: ST7789 320x240 on SPI3 ---------- */
/*
 * NOTE: LCD chip-select is NOT a GPIO. It is PCA9557 bit 0 and is kept
 * permanently low (single device on the bus).
 */
#define BSP_LCD_SPI_HOST        SPI3_HOST
#define BSP_LCD_SPI_MOSI        GPIO_NUM_40
#define BSP_LCD_SPI_CLK         GPIO_NUM_41
#define BSP_LCD_DC              GPIO_NUM_39
#define BSP_LCD_BACKLIGHT       GPIO_NUM_42   /* active LOW: 0 = full brightness */
#define BSP_LCD_H_RES           320
#define BSP_LCD_V_RES           240
#define BSP_LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)

/* ---------- SD card (SDMMC 1-line) ---------- */
#define BSP_SD_CLK_IO           GPIO_NUM_47
#define BSP_SD_CMD_IO           GPIO_NUM_48
#define BSP_SD_D0_IO            GPIO_NUM_21
#define BSP_SD_MOUNT_POINT      "/sdcard"

/* ---------- Audio (ES8311 + NS4150B) ---------- */
#define BSP_I2S_MCLK            GPIO_NUM_38
#define BSP_I2S_BCLK            GPIO_NUM_14
#define BSP_I2S_LRCK            GPIO_NUM_13
#define BSP_I2S_DOUT            GPIO_NUM_45
#define BSP_I2S_DIN             GPIO_NUM_12

/* ---------- User button ---------- */
#define BSP_BTN_BOOT            GPIO_NUM_0

#ifdef __cplusplus
}
#endif
