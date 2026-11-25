// SPDX-License-Identifier: GPL-3.0-only
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "bsp_audio.h"
#include "bsp_i2c.h"
#include "bsp_lcd.h"
#include "bsp_pca9557.h"
#include "bsp_sd.h"
#include "bsp_touch.h"
#include "holo_port.h"
#include "silt_controls.h"
#include "silt_host.h"
#include "Audio.h"
#include "Audio_Holo.h"

#define GAME_ROOT "/sdcard/siltwalk"

static const char *TAG = "silt";
int ClassiCube_HoloMain(void);

static bool folder(const char *path)
{
    if (mkdir(path, 0775) == 0) return true;
    struct stat info;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool prepare_storage(void)
{
    return folder(GAME_ROOT) && folder(GAME_ROOT "/maps") && folder(GAME_ROOT "/cache");
}

int Siltwalk_PrepareAudio(void)
{
    static bool ready;
    if (ready) return 1;

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Audio: %s", esp_err_to_name(err));
        return 0;
    }
    silt_host_set_audio_enabled(true);
    ready = true;
    return 1;
}

static void game_task(void *arg)
{
    (void)arg;
    int result = ClassiCube_HoloMain();
    Audio_HoloShutdown();
    holo_port_display_release();
    ESP_LOGW(TAG, "game loop returned %d", result);
    vTaskDeleteWithCaps(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_pca9557_init());
    ESP_ERROR_CHECK(bsp_lcd_init());
    bsp_lcd_fill(0x10c7);
    bsp_lcd_set_backlight(100);
    silt_boot_message("正在检查存储");

    esp_err_t sd = bsp_sd_init();
    silt_host_set_sd_ready(sd == ESP_OK);
    if (sd != ESP_OK || !prepare_storage()) {
        ESP_LOGE(TAG, "Unable to prepare " GAME_ROOT ": %s", esp_err_to_name(sd));
        silt_boot_message(sd == ESP_OK ? "存储初始化失败" : "请插入SD卡");
        return;
    }

    esp_err_t touch = bsp_touch_init();
    if (touch != ESP_OK) ESP_LOGW(TAG, "Touch: %s", esp_err_to_name(touch));
    silt_controls_init();
    silt_boot_message("正在准备河口");

    silt_host_set_audio_enabled(false);
    ESP_LOGI(TAG, "Audio muted until enabled");

    holo_port_set_host(silt_host_api());
    holo_port_set_root(GAME_ROOT);
    holo_port_clear_stop();
    if (!holo_port_display_acquire()) {
        ESP_LOGE(TAG, "LCD bridge: %s", holo_port_last_error());
        silt_boot_message("屏幕初始化失败");
        return;
    }

    ESP_LOGI(TAG, "starting engine, internal RAM %u, PSRAM %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (xTaskCreatePinnedToCoreWithCaps(game_task, "silt_game", 128 * 1024, NULL, 4,
                                      NULL, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        holo_port_display_release();
        silt_boot_message("内存不足");
    }
}
