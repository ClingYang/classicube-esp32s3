// SPDX-License-Identifier: MIT
/*
 * I2C master bus (new driver API, IDF v6).
 * Legacy driver/i2c.h is EOL in v6 and removed in v7 - do not use it.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up I2C0 on SDA=GPIO1 / SCL=GPIO2. Idempotent. */
esp_err_t bsp_i2c_init(void);

i2c_master_bus_handle_t bsp_i2c_bus(void);

/* Register (or reuse) a device on the bus. Returns NULL on failure. */
i2c_master_dev_handle_t bsp_i2c_dev(uint8_t dev_addr, uint32_t scl_hz);

/* Convenience wrappers (device handle resolved internally). */
esp_err_t bsp_i2c_write(uint8_t dev_addr, const uint8_t *data, size_t len);
esp_err_t bsp_i2c_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t val);
esp_err_t bsp_i2c_write_read(uint8_t dev_addr, const uint8_t *wr, size_t wr_len,
                             uint8_t *rd, size_t rd_len);

#ifdef __cplusplus
}
#endif
