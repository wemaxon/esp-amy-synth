#ifndef HAL_AUDIO_CODEC_H
#define HAL_AUDIO_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "es8388.h"
#include "es8390.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t type;
    uint8_t address;
    union {
        es8388_t es8388;
        es8390_t es8390;
    } dev;
} audio_codec_t;

const char *audio_codec_type_name(uint8_t type);
bool audio_codec_type_uses_i2c(uint8_t type);
esp_err_t audio_codec_init(audio_codec_t *codec, i2c_master_bus_handle_t bus);
esp_err_t audio_codec_start(audio_codec_t *codec);
esp_err_t audio_codec_set_volume(audio_codec_t *codec, uint8_t volume_percent);
esp_err_t audio_codec_set_mute(audio_codec_t *codec, bool mute);
esp_err_t audio_codec_configure_mic_input(audio_codec_t *codec);
esp_err_t audio_codec_set_mic_gain_db(audio_codec_t *codec, float requested_db, float *applied_db);
esp_err_t audio_codec_dump_registers(const audio_codec_t *codec, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
