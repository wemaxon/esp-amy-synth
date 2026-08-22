#include "pcf857x.h"

esp_err_t pcf857x_add_device(i2c_master_bus_handle_t bus,
                             uint8_t address,
                             uint32_t scl_speed_hz,
                             i2c_master_dev_handle_t *device)
{
    if (bus == NULL || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };
    return i2c_master_bus_add_device(bus, &config, device);
}

esp_err_t pcf857x_read(i2c_master_dev_handle_t device,
                       uint32_t timeout_ms,
                       uint8_t *bytes,
                       size_t byte_count)
{
    if (device == NULL || bytes == NULL || byte_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_receive(device, bytes, byte_count, timeout_ms);
}

esp_err_t pcf857x_write(i2c_master_dev_handle_t device,
                        uint32_t timeout_ms,
                        const uint8_t *bytes,
                        size_t byte_count)
{
    if (device == NULL || bytes == NULL || byte_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(device, bytes, byte_count, timeout_ms);
}

