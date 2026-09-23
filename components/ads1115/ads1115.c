#include "ads1115.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01

#define ADS1115_CONFIG_OS_SINGLE       0x8000
#define ADS1115_CONFIG_MUX_SINGLE_0    0x4000
#define ADS1115_CONFIG_MUX_SINGLE_1    0x5000
#define ADS1115_CONFIG_MUX_SINGLE_2    0x6000
#define ADS1115_CONFIG_MUX_SINGLE_3    0x7000
#define ADS1115_CONFIG_PGA_4V096       0x0200
#define ADS1115_CONFIG_MODE_SINGLESHOT 0x0100
#define ADS1115_CONFIG_DR_860SPS       0x00E0
#define ADS1115_CONFIG_COMP_DISABLE    0x0003

static uint16_t mux_for_channel(ads1115_channel_t channel)
{
    switch (channel) {
    case ADS1115_CHANNEL_0:
        return ADS1115_CONFIG_MUX_SINGLE_0;
    case ADS1115_CHANNEL_1:
        return ADS1115_CONFIG_MUX_SINGLE_1;
    case ADS1115_CHANNEL_2:
        return ADS1115_CONFIG_MUX_SINGLE_2;
    case ADS1115_CHANNEL_3:
        return ADS1115_CONFIG_MUX_SINGLE_3;
    default:
        return ADS1115_CONFIG_MUX_SINGLE_0;
    }
}

static esp_err_t ads1115_write_reg(const ads1115_t *dev, uint8_t reg, uint16_t value)
{
    uint8_t bytes[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };

    return i2c_master_transmit(
        dev->dev,
        bytes,
        sizeof(bytes),
        dev->timeout_ms
    );
}

static esp_err_t ads1115_read_reg(const ads1115_t *dev, uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(
        dev->dev,
        &reg,
        1,
        data,
        sizeof(data),
        dev->timeout_ms
    );
    if (err != ESP_OK) {
        return err;
    }

    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

esp_err_t ads1115_init(
    ads1115_t *dev,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t scl_speed_hz,
    uint32_t timeout_ms
)
{
    if (dev == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->timeout_ms = timeout_ms;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, &dev->dev);
}

esp_err_t ads1115_read_single_ended(const ads1115_t *dev, ads1115_channel_t channel, int16_t *raw)
{
    if (dev == NULL || raw == NULL || channel > ADS1115_CHANNEL_3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t config = ADS1115_CONFIG_OS_SINGLE |
                      mux_for_channel(channel) |
                      ADS1115_CONFIG_PGA_4V096 |
                      ADS1115_CONFIG_MODE_SINGLESHOT |
                      ADS1115_CONFIG_DR_860SPS |
                      ADS1115_CONFIG_COMP_DISABLE;

    esp_err_t err = ads1115_write_reg(dev, ADS1115_REG_CONFIG, config);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(2));

    uint16_t value = 0;
    err = ads1115_read_reg(dev, ADS1115_REG_CONVERSION, &value);
    if (err != ESP_OK) {
        return err;
    }

    int16_t signed_value = (int16_t)value;
    *raw = signed_value < 0 ? 0 : signed_value;
    return ESP_OK;
}
