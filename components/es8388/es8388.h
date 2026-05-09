#ifndef ESP_POCKET_AUDIO_ES8388_H
#define ESP_POCKET_AUDIO_ES8388_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8388_CONTROL1         0x00
#define ES8388_CONTROL2         0x01
#define ES8388_CHIPPOWER        0x02
#define ES8388_ADCPOWER         0x03
#define ES8388_DACPOWER         0x04
#define ES8388_MASTERMODE       0x08
#define ES8388_ADCCONTROL1      0x09
#define ES8388_ADCCONTROL2      0x0a
#define ES8388_ADCCONTROL3      0x0b
#define ES8388_ADCCONTROL4      0x0c
#define ES8388_ADCCONTROL5      0x0d
#define ES8388_ADCCONTROL8      0x10
#define ES8388_ADCCONTROL9      0x11
#define ES8388_DACCONTROL1      0x17
#define ES8388_DACCONTROL2      0x18
#define ES8388_DACCONTROL3      0x19
#define ES8388_DACCONTROL4      0x1a
#define ES8388_DACCONTROL5      0x1b
#define ES8388_DACCONTROL6      0x1c
#define ES8388_DACCONTROL7      0x1d
#define ES8388_DACCONTROL16     0x26
#define ES8388_DACCONTROL17     0x27
#define ES8388_DACCONTROL20     0x2a
#define ES8388_DACCONTROL21     0x2b
#define ES8388_DACCONTROL23     0x2d
#define ES8388_DACCONTROL24     0x2e
#define ES8388_DACCONTROL25     0x2f
#define ES8388_DACCONTROL26     0x30
#define ES8388_DACCONTROL27     0x31
#define ES8388_DACCONTROL30     0x34

typedef struct {
    i2c_master_dev_handle_t dev;
    uint32_t timeout_ms;
} es8388_t;

esp_err_t es8388_init(es8388_t *codec, i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_speed_hz, uint32_t timeout_ms);
esp_err_t es8388_start(es8388_t *codec);
esp_err_t es8388_set_mute(es8388_t *codec, bool mute);
esp_err_t es8388_set_volume(es8388_t *codec, uint8_t volume_percent);
esp_err_t es8388_write_reg(es8388_t *codec, uint8_t reg, uint8_t value);
esp_err_t es8388_read_reg(es8388_t *codec, uint8_t reg, uint8_t *value);
esp_err_t es8388_dump_registers(es8388_t *codec, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
