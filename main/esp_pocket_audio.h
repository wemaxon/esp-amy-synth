#ifndef ESP_POCKET_AUDIO_H
#define ESP_POCKET_AUDIO_H

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "es8388.h"
#include "pocket_oled.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t codec_i2c_bus;
    i2c_master_bus_handle_t control_i2c_bus;
    i2s_chan_handle_t i2s_tx;
    es8388_t codec;
    pocket_oled_t display;
} esp_pocket_audio_hw_t;

esp_err_t esp_pocket_audio_init(void);
const esp_pocket_audio_hw_t *esp_pocket_audio_hw(void);

#ifdef __cplusplus
}
#endif

#endif
