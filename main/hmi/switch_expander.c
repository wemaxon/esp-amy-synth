#include "hmi/switch_expander.h"

#include "config/esp_pocket_audio_hw_types.h"

const char *switch_expander_type_name(uint8_t type)
{
    switch (type) {
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8574:
        return "PCF8574";
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8575:
        return "PCF8575";
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_NONE:
        return "none";
    default:
        return "?";
    }
}

uint8_t switch_expander_type_pin_count(uint8_t type)
{
    switch (type) {
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8574:
        return 8;
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8575:
        return 16;
    default:
        return 0;
    }
}

esp_err_t switch_expander_init(
    switch_expander_t *expander,
    i2c_master_bus_handle_t bus,
    uint8_t type,
    uint8_t address,
    uint32_t scl_speed_hz,
    uint32_t timeout_ms
)
{
    if (expander == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    expander->type = type;
    expander->address = address;
    expander->pin_count = switch_expander_type_pin_count(type);

    switch (type) {
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8574:
        return pcf8574_init(&expander->dev.pcf8574, bus, address, scl_speed_hz, timeout_ms);
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8575:
        return pcf8575_init(&expander->dev.pcf8575, bus, address, scl_speed_hz, timeout_ms);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t switch_expander_read_port(switch_expander_t *expander, uint16_t *value)
{
    if (expander == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (expander->type) {
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8574: {
        uint8_t port = 0;
        esp_err_t err = pcf8574_read_port(&expander->dev.pcf8574, &port);
        if (err == ESP_OK) {
            *value = port;
        }
        return err;
    }
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8575:
        return pcf8575_read_port(&expander->dev.pcf8575, value);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t switch_expander_write_port(switch_expander_t *expander, uint16_t value)
{
    if (expander == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (expander->type) {
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8574:
        return pcf8574_write_port(&expander->dev.pcf8574, (uint8_t)value);
    case ESP_POCKET_AUDIO_SWITCH_EXPANDER_PCF8575:
        return pcf8575_write_port(&expander->dev.pcf8575, value);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
