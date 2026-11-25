/* SPDX-License-Identifier: GPL-3.0-only */
#include "silt_capture.h"

#ifdef SILT_CAPTURE_SCREEN
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "holo_port.h"
#include "silt_controls.h"

enum {
    CAPTURE_WIDTH = 320,
    CAPTURE_HEIGHT = 240,
    CAPTURE_WARMUP = 45,
    ROW_PREFIX = 5,
    ROW_TEXT = ROW_PREFIX + CAPTURE_WIDTH * 4 + 1
};

_Static_assert((int)CLASSICUBE_DISPLAY_WIDTH == (int)CAPTURE_WIDTH &&
               (int)CLASSICUBE_RENDER_HEIGHT == (int)CAPTURE_HEIGHT,
               "Capture output expects full 320x240 LCD composition");

static uint16_t *s_frame;
static unsigned s_warmup;
static int s_next_row;
static bool s_finished;

static void capture_writer(void *arg)
{
    uint16_t *pixels = arg;
    static const char hex[] = "0123456789abcdef";
    char row[ROW_TEXT];
    row[0] = 'R';
    row[4] = ':';
    row[ROW_TEXT - 1] = '\n';

    bool complete = fputs("SILT_FRAME 320 240\n", stdout) != EOF;
    for (int y = 0; y < CAPTURE_HEIGHT && complete; y++) {
        row[1] = (char)('0' + y / 100);
        row[2] = (char)('0' + y / 10 % 10);
        row[3] = (char)('0' + y % 10);
        const uint16_t *source = pixels + y * CAPTURE_WIDTH;
        for (int x = 0; x < CAPTURE_WIDTH; x++) {
            uint16_t p = source[x];
            char *bits = &row[ROW_PREFIX + x * 4];
            bits[0] = hex[p >> 12];
            bits[1] = hex[(p >> 8) & 15];
            bits[2] = hex[(p >> 4) & 15];
            bits[3] = hex[p & 15];
        }
        complete = fwrite(row, 1, sizeof(row), stdout) == sizeof(row);
        vTaskDelay(1);
    }
    if (complete) fputs("SILT_FRAME_END\n", stdout);
    else ESP_LOGW("silt_capture", "serial frame output incomplete");
    fflush(stdout);
    free(pixels);
    vTaskDelete(NULL);
}

void silt_capture_band(const uint16_t *pixels, int y0, int rows)
{
    if (s_finished) return;
    if (y0 == 0) {
        s_next_row = 0;
        if (!silt_controls_active()) {
            s_warmup = 0;
            free(s_frame);
            s_frame = NULL;
            return;
        }
        if (!s_frame) {
            if (++s_warmup < CAPTURE_WARMUP) return;
            s_frame = heap_caps_malloc(CAPTURE_WIDTH * CAPTURE_HEIGHT * sizeof(*s_frame),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_frame) {
                s_finished = true;
                ESP_LOGW("silt_capture", "frame allocation failed");
                return;
            }
        }
    }
    if (!s_frame) return;
    if (!pixels || y0 != s_next_row || rows <= 0 || rows > CAPTURE_HEIGHT - y0) {
        free(s_frame);
        s_frame = NULL;
        return;
    }

    memcpy(s_frame + y0 * CAPTURE_WIDTH, pixels,
           (size_t)CAPTURE_WIDTH * rows * sizeof(*s_frame));
    s_next_row += rows;
    if (s_next_row != CAPTURE_HEIGHT) return;

    uint16_t *frame = s_frame;
    s_frame = NULL;
    s_finished = true;
    if (xTaskCreate(capture_writer, "silt_capture", 4096, frame, 1, NULL) != pdPASS) {
        free(frame);
        ESP_LOGW("silt_capture", "writer task allocation failed");
    }
}
#endif
