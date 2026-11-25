// SPDX-License-Identifier: MIT
#include "bsp_sd.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "bsp_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "bsp_sd";

static bool s_ready;
static sdmmc_card_t *s_card;

esp_err_t bsp_sd_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;      /* slot 1 allows arbitrary pin mapping on S3 */
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk = BSP_SD_CLK_IO;
    slot_cfg.cmd = BSP_SD_CMD_IO;
    slot_cfg.d0 = BSP_SD_D0_IO;
    slot_cfg.width = 1;                 /* 1-line SD mode */
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_cfg,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "mounted at %s: %s %uMB", BSP_SD_MOUNT_POINT,
             s_card->cid.name,
             (unsigned)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024)));
    return ESP_OK;
}

bool bsp_sd_ready(void)
{
    return s_ready;
}

int bsp_sd_list_roms(char names[][BSP_SD_NAME_LEN], int max)
{
    if (!s_ready) {
        return -1;
    }

    const char *dir_path = BSP_SD_MOUNT_POINT "/roms";
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        ESP_LOGW(TAG, "%s does not exist", dir_path);
        return 0;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", dir_path);
        return 0;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max) {
        if (ent->d_type == DT_DIR) {
            continue;
        }
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcasecmp(ent->d_name + len - 4, ".nes") == 0) {
            strncpy(names[count], ent->d_name, BSP_SD_NAME_LEN - 1);
            names[count][BSP_SD_NAME_LEN - 1] = '\0';
            ++count;
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "found %d ROM(s) in %s", count, dir_path);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  [%d] %s", i, names[i]);
    }
    return count;
}
