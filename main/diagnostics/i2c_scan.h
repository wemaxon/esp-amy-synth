#ifndef DIAGNOSTICS_I2C_SCAN_H
#define DIAGNOSTICS_I2C_SCAN_H

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_i2c_scan_bus(
    i2c_master_bus_handle_t bus,
    const char *bus_name,
    uint32_t timeout_ms
);

#ifdef __cplusplus
}
#endif

#endif
