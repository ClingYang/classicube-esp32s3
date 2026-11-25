// SPDX-License-Identifier: MIT
#include "bsp_i2c.h"

#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2c";

#define MAX_DEVICES 6

static i2c_master_bus_handle_t s_bus;
static bool s_inited;

typedef struct {
    uint8_t addr;
    uint32_t scl_hz;
    i2c_master_dev_handle_t handle;
} dev_slot_t;

static dev_slot_t s_devs[MAX_DEVICES];
static int s_dev_count;

esp_err_t bsp_i2c_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BSP_I2C_PORT_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2C0 up: SDA=%d SCL=%d %dHz", BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    return s_bus;
}

i2c_master_dev_handle_t bsp_i2c_dev(uint8_t dev_addr, uint32_t scl_hz)
{
    if (!s_inited) {
        return NULL;
    }
    if (scl_hz == 0) {
        scl_hz = BSP_I2C_FREQ_HZ;
    }

    for (int i = 0; i < s_dev_count; i++) {
        if (s_devs[i].addr == dev_addr && s_devs[i].scl_hz == scl_hz) {
            return s_devs[i].handle;
        }
    }
    if (s_dev_count >= MAX_DEVICES) {
        ESP_LOGE(TAG, "device table full, cannot add 0x%02X", dev_addr);
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = scl_hz,
    };

    i2c_master_dev_handle_t handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device 0x%02X failed: %s", dev_addr, esp_err_to_name(err));
        return NULL;
    }

    s_devs[s_dev_count].addr = dev_addr;
    s_devs[s_dev_count].scl_hz = scl_hz;
    s_devs[s_dev_count].handle = handle;
    s_dev_count++;
    return handle;
}

esp_err_t bsp_i2c_write(uint8_t dev_addr, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = bsp_i2c_dev(dev_addr, 0);
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(dev, data, len, 100);
}

esp_err_t bsp_i2c_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return bsp_i2c_write(dev_addr, buf, sizeof(buf));
}

esp_err_t bsp_i2c_write_read(uint8_t dev_addr, const uint8_t *wr, size_t wr_len,
                             uint8_t *rd, size_t rd_len)
{
    i2c_master_dev_handle_t dev = bsp_i2c_dev(dev_addr, 0);
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(dev, wr, wr_len, rd, rd_len, 100);
}
