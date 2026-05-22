#include "hal/audio_codec.h"

#include "config/esp_pocket_audio_hw_types.h"
#include "esp_pocket_audio_board_config.h"

const char *audio_codec_type_name(uint8_t type)
{
    switch (type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        return "ES8388";
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
        return "PCM5102";
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return "ES8390";
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return "none";
    default:
        return "?";
    }
}

bool audio_codec_type_uses_i2c(uint8_t type)
{
    switch (type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return true;
    default:
        return false;
    }
}

esp_err_t audio_codec_init(audio_codec_t *codec, i2c_master_bus_handle_t bus)
{
    if (codec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    codec->type = ESP_POCKET_AUDIO_CODEC_TYPE;
    codec->address = ESP_POCKET_AUDIO_CODEC_I2C_ADDR;

    switch (codec->type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        if (bus == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        return es8388_init(&codec->dev.es8388,
                           bus,
                           ESP_POCKET_AUDIO_CODEC_I2C_ADDR,
                           ESP_POCKET_AUDIO_CODEC_I2C_CLK_HZ,
                           ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS);
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return ESP_OK;
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t audio_codec_start(audio_codec_t *codec)
{
    if (codec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (codec->type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        return es8388_start(&codec->dev.es8388);
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return ESP_OK;
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t audio_codec_set_volume(audio_codec_t *codec, uint8_t volume_percent)
{
    if (codec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (codec->type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        return es8388_set_volume(&codec->dev.es8388, volume_percent);
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return ESP_OK;
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t audio_codec_set_mute(audio_codec_t *codec, bool mute)
{
    if (codec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (codec->type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        return es8388_set_mute(&codec->dev.es8388, mute);
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return ESP_OK;
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t audio_codec_dump_registers(const audio_codec_t *codec, const char *tag)
{
    if (codec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (codec->type) {
    case ESP_POCKET_AUDIO_CODEC_ES8388:
        return es8388_dump_registers((es8388_t *)&codec->dev.es8388, tag);
    case ESP_POCKET_AUDIO_CODEC_PCM5102:
    case ESP_POCKET_AUDIO_CODEC_NONE:
        return ESP_OK;
    case ESP_POCKET_AUDIO_CODEC_ES8390:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}
