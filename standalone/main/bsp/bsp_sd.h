// SPDX-License-Identifier: MIT
/*
 * SD card (SDMMC 1-line) for the ShiZhanPai ESP32-S3.
 * Pins: CLK=GPIO47, CMD=GPIO48, D0=GPIO21 (DAT3 pulled up on board).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_SD_MAX_ROMS 64
#define BSP_SD_NAME_LEN 96

esp_err_t bsp_sd_init(void);

bool bsp_sd_ready(void);

/* Fills `names` with up to `max` ROM file names (no path) found under /sdcard/roms.
 * Returns the number found, or -1 if the card is not mounted. */
int bsp_sd_list_roms(char names[][BSP_SD_NAME_LEN], int max);

#ifdef __cplusplus
}
#endif
