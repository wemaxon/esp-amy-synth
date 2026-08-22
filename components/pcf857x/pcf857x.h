#ifndef PCF857X_H
#define PCF857X_H

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf857x_add_device(i2c_master_bus_handle_t bus,
                             uint8_t address,
                             uint32_t scl_speed_hz,
                             i2c_master_dev_handle_t *device);
esp_err_t pcf857x_read(i2c_master_dev_handle_t device,
                       uint32_t timeout_ms,
                       uint8_t *bytes,
                       size_t byte_count);
esp_err_t pcf857x_write(i2c_master_dev_handle_t device,
                        uint32_t timeout_ms,
                        const uint8_t *bytes,
                        size_t byte_count);

#ifdef __cplusplus
}
#endif

#endif

