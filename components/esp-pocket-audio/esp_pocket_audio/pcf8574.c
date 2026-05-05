#include "pcf8574.h"

esp_err_t pcf8574_init(
    pcf8574_t *dev,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t scl_speed_hz,
    uint32_t timeout_ms
)
{
    if (dev == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->output_latch = 0xFF;
    dev->timeout_ms = timeout_ms;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev->dev);
    if (err != ESP_OK) {
        return err;
    }

    return pcf8574_write_port(dev, 0xFF);
}

esp_err_t pcf8574_read_port(const pcf8574_t *dev, uint8_t *value)
{
    if (dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_receive(
        dev->dev,
        value,
        1,
        dev->timeout_ms
    );
}

esp_err_t pcf8574_write_port(pcf8574_t *dev, uint8_t value)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->output_latch = value;
    return i2c_master_transmit(
        dev->dev,
        &value,
        1,
        dev->timeout_ms
    );
}

esp_err_t pcf8574_read_pin(const pcf8574_t *dev, uint8_t pin, bool *level)
{
    if (dev == NULL || level == NULL || pin >= 8) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port = 0;
    esp_err_t err = pcf8574_read_port(dev, &port);
    if (err != ESP_OK) {
        return err;
    }

    *level = ((port >> pin) & 0x01) != 0;
    return ESP_OK;
}

esp_err_t pcf8574_write_pin(pcf8574_t *dev, uint8_t pin, bool level)
{
    if (dev == NULL || pin >= 8) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t value = dev->output_latch;
    if (level) {
        value |= (uint8_t)(1U << pin);
    } else {
        value &= (uint8_t)~(1U << pin);
    }

    return pcf8574_write_port(dev, value);
}
