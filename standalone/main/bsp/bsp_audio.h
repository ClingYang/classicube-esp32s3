// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BSP_AUDIO_SAMPLE_RATE 16000
#define BSP_AUDIO_DMA_DESCRIPTORS 4
#define BSP_AUDIO_DMA_FRAMES 128

esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_speaker(bool on);
esp_err_t bsp_audio_write(const int16_t *stereo, size_t frames);
