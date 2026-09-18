#ifndef ESP_POCKET_AUDIO_ES8390_H
#define ESP_POCKET_AUDIO_ES8390_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8390_REG_RESET_CONTROL        0x00
#define ES8390_REG_MISC_CONTROL         0x01
#define ES8390_REG_MASTER_CLOCK_CONTROL 0x02
#define ES8390_REG_CLOCK_OFF_1          0x03
#define ES8390_REG_MAIN_CLOCK_DIV_1     0x04
#define ES8390_REG_MASTER_CLOCK_MULT    0x05
#define ES8390_REG_MAIN_CLOCK_MUX_1     0x06
#define ES8390_REG_MAIN_CLOCK_MUX_2     0x07
#define ES8390_REG_ADC_CLOCK_CONTROL    0x08
#define ES8390_REG_DAC_CLOCK_CONTROL_1  0x09
#define ES8390_REG_DAC_CLOCK_CONTROL_2  0x0a
#define ES8390_REG_MASTER_SCLK_DIV      0x0b
#define ES8390_REG_MASTER_LRCK_DIV_1    0x0c
#define ES8390_REG_MASTER_LRCK_DIV_2    0x0d
#define ES8390_REG_CLOCK_OFF_2          0x0e
#define ES8390_REG_OSC_CLOCK_CONTROL    0x0f
#define ES8390_REG_CSM_JUMP_CONTROL     0x10
#define ES8390_REG_MAIN_CLOCK_DIV_2     0x11

#define ES8390_REG_ADC_SP_CONTROL       0x20
#define ES8390_REG_ADC_OSR_CONTROL      0x21
#define ES8390_REG_ADC_DSP_COUNTER      0x22
#define ES8390_REG_ADC_MODE_CONTROL     0x23
#define ES8390_REG_ADC_HPF_CONTROL_1    0x24
#define ES8390_REG_ADC_HPF_CONTROL_2    0x25
#define ES8390_REG_ADC_OSR_VOLUME       0x26
#define ES8390_REG_ADC1_VOLUME          0x27
#define ES8390_REG_ADC2_VOLUME          0x28
#define ES8390_REG_ALC_CONTROL_1        0x29
#define ES8390_REG_ADC_PTDM_CONTROL     0x2a
#define ES8390_REG_ADC_LRCK_SYNC        0x2f
#define ES8390_REG_ADC_RESET_CONTROL    0x31

#define ES8390_REG_DAC_SP_CONTROL       0x40
#define ES8390_REG_DAC_DSM_COUNTER      0x41
#define ES8390_REG_DAC_DSP_COUNTER      0x42
#define ES8390_REG_DAC_MISC_CONTROL_1   0x43
#define ES8390_REG_DAC_MIX_CONTROL      0x44
#define ES8390_REG_DAC_MISC_CONTROL_2   0x45
#define ES8390_REG_DAC1_VOLUME          0x46
#define ES8390_REG_DAC2_VOLUME          0x47
#define ES8390_REG_MIX_VOLUME           0x48
#define ES8390_REG_DAC_VC_RAMP_CONTROL  0x49
#define ES8390_REG_DAC_RESET_CONTROL    0x4d

#define ES8390_REG_VMID_CONTROL         0x60
#define ES8390_REG_ANALOG_ENABLE_1      0x61
#define ES8390_REG_ANALOG_SYSTEM_1      0x62
#define ES8390_REG_ANALOG_SYSTEM_2      0x63
#define ES8390_REG_ADC_ANALOG_ENABLE    0x64
#define ES8390_REG_DAC_ANALOG_CONTROL   0x69
#define ES8390_REG_ANALOG_LOW_POWER_1   0x6b
#define ES8390_REG_ANALOG_LOW_POWER_2   0x6c
#define ES8390_REG_ADC_ANALOG_CONTROL_1 0x6d
#define ES8390_REG_PGA1_ANALOG_CONTROL  0x6e
#define ES8390_REG_ADC_ANALOG_CONTROL_2 0x6f
#define ES8390_REG_ADC_ANALOG_CONTROL_3 0x70
#define ES8390_REG_ADC_ANALOG_CONTROL_4 0x71
#define ES8390_REG_PGA1_GAIN_CONTROL    0x72
#define ES8390_REG_PGA2_GAIN_CONTROL    0x73

#define ES8390_REG_CHIP_MISC_CONTROL    0xf0
#define ES8390_REG_CSM_STATE_REPORT     0xf1
#define ES8390_REG_PULL_DOWN_CONTROL    0xf2
#define ES8390_REG_ISOLATION_CONTROL    0xf3
#define ES8390_REG_CSM_STATE_CONTROL    0xf4
#define ES8390_REG_CHIP_ID1             0xfd
#define ES8390_REG_CHIP_ID0             0xfe

#define ES8390_CHIP_ID1                 0x83
#define ES8390_CHIP_ID0                 0x89
#define ES8390_DEFAULT_I2C_ADDR         0x10

typedef struct {
    i2c_master_dev_handle_t dev;
    uint32_t timeout_ms;
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
} es8390_t;

esp_err_t es8390_init(es8390_t *codec,
                      i2c_master_bus_handle_t bus,
                      uint8_t address,
                      uint32_t scl_speed_hz,
                      uint32_t timeout_ms,
                      uint32_t sample_rate_hz,
                      uint8_t bits_per_sample);
esp_err_t es8390_start(es8390_t *codec);
esp_err_t es8390_enable_dac_only_test(es8390_t *codec);
esp_err_t es8390_set_mute(es8390_t *codec, bool mute);
esp_err_t es8390_set_volume(es8390_t *codec, uint8_t volume_percent);
esp_err_t es8390_configure_rin1_input(es8390_t *codec);
esp_err_t es8390_set_rin1_gain_db(es8390_t *codec, float requested_db, float *applied_db);
esp_err_t es8390_write_reg(es8390_t *codec, uint8_t reg, uint8_t value);
esp_err_t es8390_read_reg(es8390_t *codec, uint8_t reg, uint8_t *value);
esp_err_t es8390_dump_registers(es8390_t *codec, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
