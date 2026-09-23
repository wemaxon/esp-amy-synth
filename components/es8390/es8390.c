#include "es8390.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ES8390_BIT(n)                              (1U << (n))

#define ES8390_RESET_RST_DIG                       ES8390_BIT(6)
#define ES8390_RESET_RST_XCLKGEN                   ES8390_BIT(5)
#define ES8390_RESET_RST_ADCCLK_GEN                ES8390_BIT(4)
#define ES8390_RESET_RST_DACCLK_GEN                ES8390_BIT(3)
#define ES8390_RESET_RST_ADC_DIG                   ES8390_BIT(2)
#define ES8390_RESET_RST_DAC_DIG                   ES8390_BIT(1)
#define ES8390_RESET_CSM_ON                        ES8390_BIT(0)
#define ES8390_RESET_DIGITAL_BLOCKS                (ES8390_RESET_RST_DIG | \
                                                     ES8390_RESET_RST_XCLKGEN | \
                                                     ES8390_RESET_RST_ADCCLK_GEN | \
                                                     ES8390_RESET_RST_DACCLK_GEN | \
                                                     ES8390_RESET_RST_ADC_DIG | \
                                                     ES8390_RESET_RST_DAC_DIG)
#define ES8390_RESET_RELEASE                       0x00

#define ES8390_MISC_MST_CLK_ON                     ES8390_BIT(3)

#define ES8390_CLOCK_ADC_ON                        ES8390_BIT(7)
#define ES8390_CLOCK_DAC_ON                        ES8390_BIT(6)
#define ES8390_CLOCK_EXT_BCLK_LRCK_ON              ES8390_BIT(1)
#define ES8390_CLOCK_EXT_MCLK_ON                   ES8390_BIT(0)
#define ES8390_CLOCK_ON_INIT                       (ES8390_CLOCK_ADC_ON | \
                                                     ES8390_CLOCK_DAC_ON | \
                                                     ES8390_CLOCK_EXT_MCLK_ON)
#define ES8390_CLOCK_ON_PLAYBACK                   (ES8390_CLOCK_ON_INIT | \
                                                     ES8390_CLOCK_EXT_BCLK_LRCK_ON)
#define ES8390_CLOCK_ON_DAC_ONLY_PLAYBACK          (ES8390_CLOCK_DAC_ON | \
                                                     ES8390_CLOCK_EXT_BCLK_LRCK_ON | \
                                                     ES8390_CLOCK_EXT_MCLK_ON)

#define ES8390_CSM_OSCLK_SOFT_ON                   ES8390_BIT(7)
#define ES8390_CSM_LRCK_DET_ON                     ES8390_BIT(6)
#define ES8390_CSM_STDBY2NRML                      ES8390_BIT(5)
#define ES8390_CSM_MCLKOFF_AUTO_LPEN               ES8390_BIT(2)
#define ES8390_CSM_JUMP_STANDBY_BASELINE           (ES8390_CSM_OSCLK_SOFT_ON | \
                                                     ES8390_CSM_LRCK_DET_ON | \
                                                     ES8390_CSM_MCLKOFF_AUTO_LPEN)
#define ES8390_CSM_JUMP_NORMAL_PLAYBACK            (ES8390_CSM_JUMP_STANDBY_BASELINE | \
                                                     ES8390_CSM_STDBY2NRML)

#define ES8390_SERIAL_WORD_LEN_16BIT               (3U << 5)
#define ES8390_SERIAL_WORD_LEN_18BIT               (2U << 5)
#define ES8390_SERIAL_WORD_LEN_20BIT               (1U << 5)
#define ES8390_SERIAL_WORD_LEN_24BIT               (0U << 5)
#define ES8390_SERIAL_WORD_LEN_32BIT               (4U << 5)
#define ES8390_SERIAL_FORMAT_I2S                   (0U << 2)
#define ES8390_SERIAL_MUTE_CH1                     ES8390_BIT(1)
#define ES8390_SERIAL_MUTE_CH2                     ES8390_BIT(0)
#define ES8390_SERIAL_MUTE_BOTH                    (ES8390_SERIAL_MUTE_CH1 | ES8390_SERIAL_MUTE_CH2)

#define ES8390_ADC_LRCK_SYNC_OSR_VOLUME_ON         (ES8390_BIT(7) | ES8390_BIT(6))
#define ES8390_ADC_LRCK_SYNC_DEMOUT_MUTE           (ES8390_BIT(5) | ES8390_BIT(4))
#define ES8390_DAC_MISC1_SINGLE_SPEED              0x00
#define ES8390_DAC_MIX_CONTROL_NORMAL              0x00
#define ES8390_DAC_MISC2_VPPSCALE_MINUS_0_1DB      0x03
#define ES8390_DAC_VC_RAMP_MAX                     0x0f

#define ES8390_DAC_ANALOG_SPD_DOWN2                ES8390_BIT(7)
#define ES8390_DAC_ANALOG_SPD_DOWN1                ES8390_BIT(6)
#define ES8390_DAC_ANALOG_RESERVED5                ES8390_BIT(5)
#define ES8390_DAC_ANALOG_HPRSW_HP_DRIVER          ES8390_BIT(1)
#define ES8390_DAC_ANALOG_HPLSW_HP_DRIVER          ES8390_BIT(0)
/* Line-out mode: leave both SEL paths in normal mode and keep HP drivers off. */
#define ES8390_DAC_ANALOG_LINEOUT                  ES8390_DAC_ANALOG_RESERVED5

#define ES8390_ISOLATION_REGISTER_ACCESS           0x00
#define ES8390_ISOLATION_POWER_DETECT_ENABLE       (ES8390_BIT(5) | ES8390_BIT(4) | ES8390_BIT(3))

#define ES8390_ANALOG_ENABLE_EN_ANA                ES8390_BIT(7)
#define ES8390_ANALOG_ENABLE_EN_IBIASGEN           ES8390_BIT(6)
#define ES8390_ANALOG_ENABLE_EN_DACVREFGEN         ES8390_BIT(5)
#define ES8390_ANALOG_ENABLE_RESERVED4             ES8390_BIT(4)
#define ES8390_ANALOG_ENABLE_IBIAS_LEVEL2          (2U << 2)
#define ES8390_ANALOG_ENABLE_VMID_LEVEL1           1U
#define ES8390_ANALOG_ENABLE_INIT                  (ES8390_ANALOG_ENABLE_EN_ANA | \
                                                     ES8390_ANALOG_ENABLE_EN_IBIASGEN | \
                                                     ES8390_ANALOG_ENABLE_IBIAS_LEVEL2 | \
                                                     ES8390_ANALOG_ENABLE_VMID_LEVEL1)
#define ES8390_ANALOG_ENABLE_PLAYBACK              (ES8390_ANALOG_ENABLE_INIT | \
                                                     ES8390_ANALOG_ENABLE_EN_DACVREFGEN | \
                                                     ES8390_ANALOG_ENABLE_RESERVED4)

#define ES8390_ADC_ANALOG_MOD1_RST                 ES8390_BIT(5)
#define ES8390_ADC_ANALOG_MOD2_RST                 ES8390_BIT(4)
#define ES8390_ADC_ANALOG_REFERENCE_ON             ES8390_BIT(7)
#define ES8390_ADC_ANALOG_MOD2_ON                  ES8390_BIT(2)
#define ES8390_ADC_ANALOG_PGA2_ON                  ES8390_BIT(0)
#define ES8390_ADC_ANALOG_RIN1_ON                  (ES8390_ADC_ANALOG_REFERENCE_ON | \
                                                     ES8390_ADC_ANALOG_MOD2_ON | \
                                                     ES8390_ADC_ANALOG_PGA2_ON)
#define ES8390_ADC_ANALOG_RELEASED                 0x00
#define ES8390_ADC_ANALOG_MODULATORS_POWERDOWN     (ES8390_ADC_ANALOG_MOD1_RST | ES8390_ADC_ANALOG_MOD2_RST)
#define ES8390_ADC_MODE_AMIC                       0x00
#define ES8390_PGA_GAIN_NO_INPUT                   0x00
#define ES8390_PGA2_SINGLE_ENDED                   ES8390_BIT(6)
#define ES8390_PGA2_INPUT_1                        ES8390_BIT(4)
#define ES8390_ADC_RESET_CFDSP                     ES8390_BIT(2)
#define ES8390_ADC_RESET_ALC                       ES8390_BIT(1)
#define ES8390_ADC_RESET_P2S                       ES8390_BIT(0)
#define ES8390_ADC_RESET_DIGITAL_PATH              (ES8390_ADC_RESET_CFDSP | \
                                                     ES8390_ADC_RESET_ALC | \
                                                     ES8390_ADC_RESET_P2S)

#define ES8390_VOLUME_0DB                          0xbf
#define ES8390_MIX_VOLUME_0DB                      0xbe

#define ES8390_DAC_RESET_VENDOR_READY              0x02
#define ES8390_CHIP_MISC_BASELINE                  0x12
#define ES8390_VMID_VENDOR_INIT                    0x2a
#define ES8390_ANALOG_SYSTEM1_VENDOR_INIT          0x4f
#define ES8390_ANALOG_SYSTEM2_VENDOR_INIT          0x06
#define ES8390_ANALOG_LOW_POWER_NORMAL             0x00
#define ES8390_ADC_ANALOG_CONTROL1_VENDOR_INIT     0x16
#define ES8390_PGA1_ANALOG_VENDOR_INIT             0xaa
#define ES8390_ADC_ANALOG_CONTROL2_VENDOR_INIT     0x66
#define ES8390_ADC_ANALOG_CONTROL3_VENDOR_INIT     0x99
#define ES8390_ADC_HPF1_VENDOR_INIT                0x64
#define ES8390_ADC_HPF2_VENDOR_INIT                0x04
#define ES8390_ADC_HPF1_PLAYBACK                   0x6a
#define ES8390_ADC_HPF2_PLAYBACK                   0x0a

typedef struct {
    uint8_t reg;
    uint8_t value;
} es8390_reg_value_t;

typedef struct {
    uint32_t sample_rate_hz;
    es8390_reg_value_t regs[14];
} es8390_clock_config_t;

static const char *TAG = "ES8390";

typedef struct {
    float db;
    uint8_t code;
} es8390_pga_gain_t;

/* Register 0x73 has no valid encodings for codes 4 and 5. */
static const es8390_pga_gain_t s_pga2_gains[] = {
    {0.0f, 0x0}, {3.5f, 0x1}, {6.5f, 0x2}, {9.5f, 0x3},
    {12.5f, 0x6}, {15.5f, 0x7}, {18.5f, 0x8}, {21.5f, 0x9},
    {24.5f, 0xa}, {27.5f, 0xb}, {30.5f, 0xc}, {33.5f, 0xd},
    {36.5f, 0xe},
};

static const es8390_reg_value_t s_analog_init_regs[] = {
    {ES8390_REG_ADC_HPF_CONTROL_1, ES8390_ADC_HPF1_VENDOR_INIT},
    {ES8390_REG_ADC_HPF_CONTROL_2, ES8390_ADC_HPF2_VENDOR_INIT},
    {ES8390_REG_DAC_MISC_CONTROL_2, ES8390_DAC_MISC2_VPPSCALE_MINUS_0_1DB},
    {ES8390_REG_VMID_CONTROL, ES8390_VMID_VENDOR_INIT},
    {ES8390_REG_ANALOG_ENABLE_1, ES8390_ANALOG_ENABLE_INIT},
    {ES8390_REG_ANALOG_SYSTEM_1, ES8390_ANALOG_SYSTEM1_VENDOR_INIT},
    {ES8390_REG_ANALOG_SYSTEM_2, ES8390_ANALOG_SYSTEM2_VENDOR_INIT},
    {ES8390_REG_ANALOG_LOW_POWER_1, ES8390_ANALOG_LOW_POWER_NORMAL},
    {ES8390_REG_ADC_ANALOG_CONTROL_1, ES8390_ADC_ANALOG_CONTROL1_VENDOR_INIT},
    {ES8390_REG_PGA1_ANALOG_CONTROL, ES8390_PGA1_ANALOG_VENDOR_INIT},
    {ES8390_REG_ADC_ANALOG_CONTROL_2, ES8390_ADC_ANALOG_CONTROL2_VENDOR_INIT},
    {ES8390_REG_ADC_ANALOG_CONTROL_3, ES8390_ADC_ANALOG_CONTROL3_VENDOR_INIT},
    {ES8390_REG_ADC_MODE_CONTROL, ES8390_ADC_MODE_AMIC},
    {ES8390_REG_PGA1_GAIN_CONTROL, ES8390_PGA_GAIN_NO_INPUT},
    {ES8390_REG_PGA2_GAIN_CONTROL, ES8390_PGA_GAIN_NO_INPUT},
    {ES8390_REG_CSM_JUMP_CONTROL, ES8390_CSM_JUMP_STANDBY_BASELINE},
    {ES8390_REG_MISC_CONTROL, ES8390_MISC_MST_CLK_ON},
};

static const es8390_reg_value_t s_volume_init_regs[] = {
    {ES8390_REG_ADC_OSR_VOLUME, ES8390_VOLUME_0DB},
    {ES8390_REG_ADC1_VOLUME, ES8390_VOLUME_0DB},
    {ES8390_REG_ADC2_VOLUME, ES8390_VOLUME_0DB},
    {ES8390_REG_DAC_MIX_CONTROL, ES8390_DAC_MIX_CONTROL_NORMAL},
    {ES8390_REG_DAC1_VOLUME, ES8390_VOLUME_0DB},
    {ES8390_REG_DAC2_VOLUME, ES8390_VOLUME_0DB},
    {ES8390_REG_MIX_VOLUME, ES8390_MIX_VOLUME_0DB},
    {ES8390_REG_DAC_VC_RAMP_CONTROL, ES8390_DAC_VC_RAMP_MAX},
};

static const es8390_reg_value_t s_init_complete_regs[] = {
    {ES8390_REG_RESET_CONTROL, ES8390_RESET_RELEASE},
    {ES8390_REG_CLOCK_OFF_1, ES8390_CLOCK_ON_INIT},
    {ES8390_REG_RESET_CONTROL, ES8390_RESET_CSM_ON},
    {ES8390_REG_DAC_RESET_CONTROL, ES8390_DAC_RESET_VENDOR_READY},
};

static const es8390_reg_value_t s_playback_start_regs[] = {
    {ES8390_REG_DAC_RESET_CONTROL, ES8390_DAC_RESET_VENDOR_READY},
    {ES8390_REG_DAC_ANALOG_CONTROL, ES8390_DAC_ANALOG_LINEOUT},
    {ES8390_REG_ANALOG_ENABLE_1, ES8390_ANALOG_ENABLE_PLAYBACK},
    {ES8390_REG_ADC_ANALOG_ENABLE, ES8390_ADC_ANALOG_RELEASED},
    {ES8390_REG_CSM_JUMP_CONTROL, ES8390_CSM_JUMP_NORMAL_PLAYBACK},
    {ES8390_REG_RESET_CONTROL, ES8390_RESET_CSM_ON},
    {ES8390_REG_CLOCK_OFF_1, ES8390_CLOCK_ON_PLAYBACK},
    {ES8390_REG_ADC_HPF_CONTROL_1, ES8390_ADC_HPF1_PLAYBACK},
    {ES8390_REG_ADC_HPF_CONTROL_2, ES8390_ADC_HPF2_PLAYBACK},
};

static const es8390_reg_value_t s_dac_only_test_regs[] = {
    {ES8390_REG_ADC_LRCK_SYNC, ES8390_ADC_LRCK_SYNC_DEMOUT_MUTE},
    {ES8390_REG_ADC_RESET_CONTROL, ES8390_ADC_RESET_DIGITAL_PATH},
    {ES8390_REG_DAC_MIX_CONTROL, ES8390_DAC_MIX_CONTROL_NORMAL},
    {ES8390_REG_PGA1_GAIN_CONTROL, ES8390_PGA_GAIN_NO_INPUT},
    {ES8390_REG_PGA2_GAIN_CONTROL, ES8390_PGA_GAIN_NO_INPUT},
    {ES8390_REG_ADC_ANALOG_ENABLE, ES8390_ADC_ANALOG_MODULATORS_POWERDOWN},
    {ES8390_REG_CLOCK_OFF_1, ES8390_CLOCK_ON_DAC_ONLY_PLAYBACK},
};

static const es8390_clock_config_t s_clock_configs[] = {
    {
        .sample_rate_hz = 44100,
        .regs = {
            {ES8390_REG_MASTER_CLOCK_CONTROL, 0x00},
            {ES8390_REG_MAIN_CLOCK_DIV_1, 0x01},
            {ES8390_REG_MASTER_CLOCK_MULT, 0x41},
            {ES8390_REG_MAIN_CLOCK_MUX_1, 0x04},
            {ES8390_REG_MAIN_CLOCK_MUX_2, 0xd0},
            {ES8390_REG_ADC_CLOCK_CONTROL, 0x10},
            {ES8390_REG_DAC_CLOCK_CONTROL_1, 0xd1},
            {ES8390_REG_DAC_CLOCK_CONTROL_2, 0x80},
            {ES8390_REG_OSC_CLOCK_CONTROL, 0x40},
            {ES8390_REG_MAIN_CLOCK_DIV_2, 0x00},
            {ES8390_REG_ADC_OSR_CONTROL, 0x1f},
            {ES8390_REG_ADC_DSP_COUNTER, 0x7f},
            {ES8390_REG_DAC_DSM_COUNTER, 0x7f},
            {ES8390_REG_DAC_DSP_COUNTER, 0x7f},
        },
    },
    {
        .sample_rate_hz = 48000,
        .regs = {
            {ES8390_REG_MASTER_CLOCK_CONTROL, 0x00},
            {ES8390_REG_MAIN_CLOCK_DIV_1, 0x01},
            {ES8390_REG_MASTER_CLOCK_MULT, 0x41},
            {ES8390_REG_MAIN_CLOCK_MUX_1, 0x04},
            {ES8390_REG_MAIN_CLOCK_MUX_2, 0xd0},
            {ES8390_REG_ADC_CLOCK_CONTROL, 0x10},
            {ES8390_REG_DAC_CLOCK_CONTROL_1, 0xd1},
            {ES8390_REG_DAC_CLOCK_CONTROL_2, 0x80},
            {ES8390_REG_OSC_CLOCK_CONTROL, 0x40},
            {ES8390_REG_MAIN_CLOCK_DIV_2, 0x00},
            {ES8390_REG_ADC_OSR_CONTROL, 0x1f},
            {ES8390_REG_ADC_DSP_COUNTER, 0x7f},
            {ES8390_REG_DAC_DSM_COUNTER, 0x7f},
            {ES8390_REG_DAC_DSP_COUNTER, 0x7f},
        },
    },
};

static const uint8_t s_dump_regs[] = {
    ES8390_REG_RESET_CONTROL,
    ES8390_REG_MISC_CONTROL,
    ES8390_REG_MASTER_CLOCK_CONTROL,
    ES8390_REG_CLOCK_OFF_1,
    ES8390_REG_MAIN_CLOCK_DIV_1,
    ES8390_REG_MASTER_CLOCK_MULT,
    ES8390_REG_MAIN_CLOCK_MUX_1,
    ES8390_REG_MAIN_CLOCK_MUX_2,
    ES8390_REG_ADC_CLOCK_CONTROL,
    ES8390_REG_DAC_CLOCK_CONTROL_1,
    ES8390_REG_DAC_CLOCK_CONTROL_2,
    ES8390_REG_OSC_CLOCK_CONTROL,
    ES8390_REG_CSM_JUMP_CONTROL,
    ES8390_REG_MAIN_CLOCK_DIV_2,
    ES8390_REG_ADC_SP_CONTROL,
    ES8390_REG_ADC_OSR_CONTROL,
    ES8390_REG_ADC_DSP_COUNTER,
    ES8390_REG_ADC_MODE_CONTROL,
    ES8390_REG_ADC_HPF_CONTROL_1,
    ES8390_REG_ADC_HPF_CONTROL_2,
    ES8390_REG_ADC_OSR_VOLUME,
    ES8390_REG_ADC1_VOLUME,
    ES8390_REG_ADC2_VOLUME,
    ES8390_REG_ADC_LRCK_SYNC,
    ES8390_REG_ADC_RESET_CONTROL,
    ES8390_REG_DAC_SP_CONTROL,
    ES8390_REG_DAC_DSM_COUNTER,
    ES8390_REG_DAC_DSP_COUNTER,
    ES8390_REG_DAC_MISC_CONTROL_1,
    ES8390_REG_DAC_MIX_CONTROL,
    ES8390_REG_DAC_MISC_CONTROL_2,
    ES8390_REG_DAC1_VOLUME,
    ES8390_REG_DAC2_VOLUME,
    ES8390_REG_MIX_VOLUME,
    ES8390_REG_DAC_VC_RAMP_CONTROL,
    ES8390_REG_DAC_RESET_CONTROL,
    ES8390_REG_VMID_CONTROL,
    ES8390_REG_ANALOG_ENABLE_1,
    ES8390_REG_ANALOG_SYSTEM_1,
    ES8390_REG_ANALOG_SYSTEM_2,
    ES8390_REG_ADC_ANALOG_ENABLE,
    ES8390_REG_DAC_ANALOG_CONTROL,
    ES8390_REG_ANALOG_LOW_POWER_1,
    ES8390_REG_ANALOG_LOW_POWER_2,
    ES8390_REG_ADC_ANALOG_CONTROL_1,
    ES8390_REG_PGA1_ANALOG_CONTROL,
    ES8390_REG_ADC_ANALOG_CONTROL_2,
    ES8390_REG_ADC_ANALOG_CONTROL_3,
    ES8390_REG_ADC_ANALOG_CONTROL_4,
    ES8390_REG_PGA1_GAIN_CONTROL,
    ES8390_REG_PGA2_GAIN_CONTROL,
    ES8390_REG_CHIP_MISC_CONTROL,
    ES8390_REG_CSM_STATE_REPORT,
    ES8390_REG_PULL_DOWN_CONTROL,
    ES8390_REG_ISOLATION_CONTROL,
    ES8390_REG_CSM_STATE_CONTROL,
    ES8390_REG_CHIP_ID1,
    ES8390_REG_CHIP_ID0,
};

static esp_err_t write_regs(es8390_t *codec, const es8390_reg_value_t *regs, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = es8390_write_reg(codec, regs[i].reg, regs[i].value);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t update_bits(es8390_t *codec, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old_value = 0;
    esp_err_t err = es8390_read_reg(codec, reg, &old_value);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t new_value = (old_value & ~mask) | (value & mask);
    if (new_value == old_value) {
        return ESP_OK;
    }
    return es8390_write_reg(codec, reg, new_value);
}

static uint8_t serial_word_length(uint8_t bits_per_sample)
{
    switch (bits_per_sample) {
    case 16:
        return ES8390_SERIAL_WORD_LEN_16BIT;
    case 18:
        return ES8390_SERIAL_WORD_LEN_18BIT;
    case 20:
        return ES8390_SERIAL_WORD_LEN_20BIT;
    case 24:
        return ES8390_SERIAL_WORD_LEN_24BIT;
    case 32:
        return ES8390_SERIAL_WORD_LEN_32BIT;
    default:
        ESP_LOGW(TAG, "unsupported word length %u, using 16-bit", bits_per_sample);
        return ES8390_SERIAL_WORD_LEN_16BIT;
    }
}

static esp_err_t configure_serial_ports(es8390_t *codec)
{
    uint8_t serial = serial_word_length(codec->bits_per_sample) | ES8390_SERIAL_FORMAT_I2S;
    esp_err_t err = es8390_write_reg(codec, ES8390_REG_ADC_SP_CONTROL, serial | ES8390_SERIAL_MUTE_BOTH);
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_DAC_SP_CONTROL, serial);
    }
    return err;
}

static const es8390_clock_config_t *find_clock_config(uint32_t sample_rate_hz)
{
    for (size_t i = 0; i < sizeof(s_clock_configs) / sizeof(s_clock_configs[0]); ++i) {
        if (s_clock_configs[i].sample_rate_hz == sample_rate_hz) {
            return &s_clock_configs[i];
        }
    }
    return NULL;
}

static esp_err_t configure_clocking(es8390_t *codec)
{
    const es8390_clock_config_t *config = find_clock_config(codec->sample_rate_hz);
    if (config == NULL) {
        ESP_LOGW(TAG, "unsupported sample rate %" PRIu32 " Hz, using 44.1 kHz codec clock profile",
                 codec->sample_rate_hz);
        config = &s_clock_configs[0];
    }

    esp_err_t err = write_regs(codec, config->regs, sizeof(config->regs) / sizeof(config->regs[0]));
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_ADC_LRCK_SYNC, ES8390_ADC_LRCK_SYNC_OSR_VOLUME_ON);
    }
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_DAC_MISC_CONTROL_1, ES8390_DAC_MISC1_SINGLE_SPEED);
    }
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_CHIP_MISC_CONTROL, ES8390_CHIP_MISC_BASELINE);
    }
    return err;
}

static esp_err_t check_chip_id(es8390_t *codec)
{
    uint8_t id1 = 0;
    uint8_t id0 = 0;
    esp_err_t err = es8390_read_reg(codec, ES8390_REG_CHIP_ID1, &id1);
    if (err == ESP_OK) {
        err = es8390_read_reg(codec, ES8390_REG_CHIP_ID0, &id0);
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "chip id: 0x%02x 0x%02x", id1, id0);
    if (id1 != ES8390_CHIP_ID1 || id0 != ES8390_CHIP_ID0) {
        ESP_LOGE(TAG, "unexpected chip id: got 0x%02x 0x%02x, expected 0x%02x 0x%02x",
                 id1, id0, ES8390_CHIP_ID1, ES8390_CHIP_ID0);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t enter_register_access(es8390_t *codec)
{
    return es8390_write_reg(codec, ES8390_REG_ISOLATION_CONTROL, ES8390_ISOLATION_REGISTER_ACCESS);
}

static esp_err_t reset_for_register_init(es8390_t *codec)
{
    esp_err_t err = enter_register_access(codec);
    if (err == ESP_OK) {
        err = check_chip_id(codec);
    }
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_RESET_CONTROL, ES8390_RESET_DIGITAL_BLOCKS);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        err = es8390_write_reg(codec, ES8390_REG_ISOLATION_CONTROL, ES8390_ISOLATION_POWER_DETECT_ENABLE);
    }
    return err;
}

static esp_err_t set_bias_on(es8390_t *codec)
{
    return write_regs(codec,
                      s_playback_start_regs,
                      sizeof(s_playback_start_regs) / sizeof(s_playback_start_regs[0]));
}

esp_err_t es8390_write_reg(es8390_t *codec, uint8_t reg, uint8_t value)
{
    if (codec == NULL || codec->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2] = {reg, value};
    esp_err_t err = i2c_master_transmit(codec->dev, data, sizeof(data), codec->timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write reg 0x%02x = 0x%02x failed: %s", reg, value, esp_err_to_name(err));
    }
    return err;
}

esp_err_t es8390_read_reg(es8390_t *codec, uint8_t reg, uint8_t *value)
{
    if (codec == NULL || codec->dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_master_transmit_receive(codec->dev, &reg, 1, value, 1, codec->timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read reg 0x%02x failed: %s", reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t es8390_set_mute(es8390_t *codec, bool mute)
{
    return update_bits(codec,
                       ES8390_REG_DAC_SP_CONTROL,
                       ES8390_SERIAL_MUTE_BOTH,
                       mute ? ES8390_SERIAL_MUTE_BOTH : 0);
}

esp_err_t es8390_set_volume(es8390_t *codec, uint8_t volume_percent)
{
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    uint8_t reg = (uint8_t)(((uint32_t)volume_percent * ES8390_VOLUME_0DB) / 100U);
    esp_err_t err = es8390_write_reg(codec, ES8390_REG_DAC1_VOLUME, reg);
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_DAC2_VOLUME, reg);
    }
    return err;
}

esp_err_t es8390_configure_rin1_input(es8390_t *codec)
{
    if (codec == NULL || codec->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t serial = serial_word_length(codec->bits_per_sample) |
                     ES8390_SERIAL_FORMAT_I2S |
                     ES8390_SERIAL_MUTE_CH1;
    esp_err_t err = es8390_write_reg(codec, ES8390_REG_ADC_SP_CONTROL, serial);
    if (err == ESP_OK) {
        err = es8390_write_reg(codec,
                               ES8390_REG_PGA2_GAIN_CONTROL,
                               ES8390_PGA2_SINGLE_ENDED | ES8390_PGA2_INPUT_1);
    }
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_ADC_ANALOG_ENABLE, ES8390_ADC_ANALOG_RIN1_ON);
    }
    if (err == ESP_OK) {
        err = es8390_write_reg(codec, ES8390_REG_ADC_RESET_CONTROL, 0x00);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RIN1 single-ended input enabled on ADC2 at 0.0 dB PGA gain");
    }
    return err;
}

esp_err_t es8390_set_rin1_gain_db(es8390_t *codec, float requested_db, float *applied_db)
{
    if (codec == NULL || codec->dev == NULL || !isfinite(requested_db)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t selected = 0;
    for (size_t i = 1; i < sizeof(s_pga2_gains) / sizeof(s_pga2_gains[0]); ++i) {
        if (s_pga2_gains[i].db > requested_db) {
            break;
        }
        selected = i;
    }

    esp_err_t err = es8390_write_reg(codec,
                                     ES8390_REG_PGA2_GAIN_CONTROL,
                                     ES8390_PGA2_SINGLE_ENDED |
                                         ES8390_PGA2_INPUT_1 |
                                         s_pga2_gains[selected].code);
    if (err == ESP_OK && applied_db != NULL) {
        *applied_db = s_pga2_gains[selected].db;
    }
    return err;
}

esp_err_t es8390_start(es8390_t *codec)
{
    if (codec == NULL || codec->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return set_bias_on(codec);
}

esp_err_t es8390_enable_dac_only_test(es8390_t *codec)
{
    if (codec == NULL || codec->dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t adc_serial = serial_word_length(codec->bits_per_sample) |
                         ES8390_SERIAL_FORMAT_I2S |
                         ES8390_SERIAL_MUTE_BOTH;
    esp_err_t err = es8390_write_reg(codec, ES8390_REG_ADC_SP_CONTROL, adc_serial);
    if (err == ESP_OK) {
        err = write_regs(codec,
                         s_dac_only_test_regs,
                         sizeof(s_dac_only_test_regs) / sizeof(s_dac_only_test_regs[0]));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "strict DAC-only test mode enabled");
    }
    return err;
}

esp_err_t es8390_init(es8390_t *codec,
                      i2c_master_bus_handle_t bus,
                      uint8_t address,
                      uint32_t scl_speed_hz,
                      uint32_t timeout_ms,
                      uint32_t sample_rate_hz,
                      uint8_t bits_per_sample)
{
    if (codec == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    codec->dev = NULL;

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
    codec->sample_rate_hz = sample_rate_hz;
    codec->bits_per_sample = bits_per_sample;

    err = reset_for_register_init(codec);
    if (err == ESP_OK) {
        err = write_regs(codec,
                         s_analog_init_regs,
                         sizeof(s_analog_init_regs) / sizeof(s_analog_init_regs[0]));
    }
    if (err == ESP_OK) {
        err = configure_serial_ports(codec);
    }
    if (err == ESP_OK) {
        err = configure_clocking(codec);
    }
    if (err == ESP_OK) {
        err = write_regs(codec,
                         s_volume_init_regs,
                         sizeof(s_volume_init_regs) / sizeof(s_volume_init_regs[0]));
    }
    if (err == ESP_OK) {
        err = write_regs(codec,
                         s_init_complete_regs,
                         sizeof(s_init_complete_regs) / sizeof(s_init_complete_regs[0]));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ES8390 register init complete");
        return ESP_OK;
    }

    if (codec->dev != NULL) {
        (void)i2c_master_bus_rm_device(codec->dev);
        codec->dev = NULL;
    }
    return err;
}

esp_err_t es8390_dump_registers(es8390_t *codec, const char *tag)
{
    const char *log_tag = tag != NULL ? tag : TAG;
    ESP_LOGI(log_tag, "ES8390 register dump");
    for (size_t i = 0; i < sizeof(s_dump_regs) / sizeof(s_dump_regs[0]); ++i) {
        uint8_t value = 0;
        esp_err_t err = es8390_read_reg(codec, s_dump_regs[i], &value);
        if (err == ESP_OK) {
            ESP_LOGI(log_tag, "  0x%02x = 0x%02x", s_dump_regs[i], value);
        } else {
            ESP_LOGW(log_tag, "  0x%02x read failed: %s", s_dump_regs[i], esp_err_to_name(err));
        }
    }
    return ESP_OK;
}
