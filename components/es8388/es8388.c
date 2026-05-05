#include "es8388.h"

#include "esp_log.h"

#define ES8388_ADC_INPUT_LINPUT1_RINPUT1 0x00
#define ES8388_DAC_OUTPUT_ALL            0x3c
#define ES8388_OUTPUT_VOLUME_0DB         0x1e

static const char *TAG = "ES8388";

static esp_err_t set_adc_dac_volume(es8388_t *codec, bool adc, bool dac, int volume_db, int dot)
{
    if (volume_db < -96) {
        volume_db = -96;
    } else if (volume_db > 0) {
        volume_db = 0;
    }

    uint8_t reg_value = (uint8_t)((-volume_db << 1) + (dot >= 5 ? 1 : 0));
    esp_err_t err = ESP_OK;
    if (adc) {
        err |= es8388_write_reg(codec, ES8388_ADCCONTROL8, reg_value);
        err |= es8388_write_reg(codec, ES8388_ADCCONTROL9, reg_value);
    }
    if (dac) {
        err |= es8388_write_reg(codec, ES8388_DACCONTROL5, reg_value);
        err |= es8388_write_reg(codec, ES8388_DACCONTROL4, reg_value);
    }
    return err;
}

esp_err_t es8388_write_reg(es8388_t *codec, uint8_t reg, uint8_t value)
{
    if (codec == NULL || codec->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(codec->dev, data, sizeof(data), codec->timeout_ms);
}

esp_err_t es8388_read_reg(es8388_t *codec, uint8_t reg, uint8_t *value)
{
    if (codec == NULL || codec->dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(codec->dev, &reg, 1, value, 1, codec->timeout_ms);
}

esp_err_t es8388_set_mute(es8388_t *codec, bool mute)
{
    uint8_t reg = 0;
    esp_err_t err = es8388_read_reg(codec, ES8388_DACCONTROL3, &reg);
    if (err != ESP_OK) {
        return err;
    }

    reg = (reg & 0xfb) | ((uint8_t)mute << 2);
    return es8388_write_reg(codec, ES8388_DACCONTROL3, reg);
}

esp_err_t es8388_set_volume(es8388_t *codec, uint8_t volume_percent)
{
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    uint8_t reg = (uint8_t)(((100U - volume_percent) * 0xc0U) / 100U);
    esp_err_t err = es8388_write_reg(codec, ES8388_DACCONTROL5, reg);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL4, reg);
    return err;
}

esp_err_t es8388_start(es8388_t *codec)
{
    uint8_t before = 0;
    uint8_t after = 0;
    esp_err_t err = es8388_read_reg(codec, ES8388_DACCONTROL21, &before);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL21, 0x80);
    err |= es8388_read_reg(codec, ES8388_DACCONTROL21, &after);
    if (err != ESP_OK) {
        return err;
    }

    if (before != after) {
        err |= es8388_write_reg(codec, ES8388_CHIPPOWER, 0xf0);
        err |= es8388_write_reg(codec, ES8388_CHIPPOWER, 0x00);
    }
    err |= es8388_write_reg(codec, ES8388_DACPOWER, ES8388_DAC_OUTPUT_ALL);
    err |= es8388_set_mute(codec, false);
    return err;
}

esp_err_t es8388_init(es8388_t *codec, i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_speed_hz, uint32_t timeout_ms)
{
    if (codec == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &codec->dev);
    if (err != ESP_OK) {
        return err;
    }
    codec->timeout_ms = timeout_ms;

    err |= es8388_write_reg(codec, ES8388_DACCONTROL3, 0x04);
    err |= es8388_write_reg(codec, ES8388_CONTROL2, 0x50);
    err |= es8388_write_reg(codec, ES8388_CHIPPOWER, 0x00);
    err |= es8388_write_reg(codec, 0x35, 0xa0);
    err |= es8388_write_reg(codec, 0x37, 0xd0);
    err |= es8388_write_reg(codec, 0x39, 0xd0);
    err |= es8388_write_reg(codec, ES8388_MASTERMODE, 0x00);

    err |= es8388_write_reg(codec, ES8388_DACPOWER, 0xc0);
    err |= es8388_write_reg(codec, ES8388_CONTROL1, 0x12);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL1, 0x18);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL2, 0x02);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL16, 0x00);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL17, 0x90);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL20, 0x90);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL21, 0x80);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL23, 0x00);
    err |= set_adc_dac_volume(codec, false, true, 0, 0);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL24, ES8388_OUTPUT_VOLUME_0DB);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL25, ES8388_OUTPUT_VOLUME_0DB);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL26, ES8388_OUTPUT_VOLUME_0DB);
    err |= es8388_write_reg(codec, ES8388_DACCONTROL27, ES8388_OUTPUT_VOLUME_0DB);
    err |= es8388_write_reg(codec, ES8388_DACPOWER, ES8388_DAC_OUTPUT_ALL);

    err |= es8388_write_reg(codec, ES8388_ADCPOWER, 0xff);
    err |= es8388_write_reg(codec, ES8388_ADCCONTROL1, 0xbb);
    err |= es8388_write_reg(codec, ES8388_ADCCONTROL2, ES8388_ADC_INPUT_LINPUT1_RINPUT1);
    err |= es8388_write_reg(codec, ES8388_ADCCONTROL3, 0x02);
    err |= es8388_write_reg(codec, ES8388_ADCCONTROL4, 0x0c);
    err |= es8388_write_reg(codec, ES8388_ADCCONTROL5, 0x02);
    err |= set_adc_dac_volume(codec, true, false, 0, 0);
    err |= es8388_write_reg(codec, ES8388_ADCPOWER, 0x09);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ES8388 register init complete");
    }
    return err;
}

esp_err_t es8388_dump_registers(es8388_t *codec, const char *tag)
{
    const char *log_tag = tag != NULL ? tag : TAG;
    ESP_LOGI(log_tag, "ES8388 register dump");
    for (uint8_t reg = 0; reg <= ES8388_DACCONTROL30; ++reg) {
        uint8_t value = 0;
        esp_err_t err = es8388_read_reg(codec, reg, &value);
        if (err != ESP_OK) {
            ESP_LOGW(log_tag, "reg 0x%02x read failed: %s", reg, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(log_tag, "  0x%02x = 0x%02x", reg, value);
    }
    return ESP_OK;
}
