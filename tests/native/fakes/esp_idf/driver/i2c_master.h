#ifndef HOST_STUB_I2C_MASTER_H
#define HOST_STUB_I2C_MASTER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;

typedef enum {
    I2C_ADDR_BIT_LEN_7 = 0,
} i2c_addr_bit_len_t;

typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
                             uint8_t *bytes,
                             size_t byte_count,
                             uint32_t timeout_ms);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *bytes,
                              size_t byte_count,
                              uint32_t timeout_ms);

#endif
