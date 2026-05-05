#ifndef ESP_POCKET_AUDIO_BOARD_CONFIG_H
#define ESP_POCKET_AUDIO_BOARD_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2s_types.h"
#include "driver/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ES8388 codec control bus on the ESP32-A1S module. */
#define ESP_POCKET_AUDIO_CODEC_I2C_PORT          I2C_NUM_0
#define ESP_POCKET_AUDIO_CODEC_I2C_SDA_GPIO      GPIO_NUM_33
#define ESP_POCKET_AUDIO_CODEC_I2C_SCL_GPIO      GPIO_NUM_32
#define ESP_POCKET_AUDIO_CODEC_I2C_CLK_HZ        100000
#define ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS    50
/* ESP-ADF's ES8388_ADDR value is 0x20 because it uses the 8-bit wire address.
 * ESP-IDF's i2c_master driver expects the 7-bit device address directly.
 */
#define ESP_POCKET_AUDIO_ES8388_ADDR             0x10

/* ES8388 I2S pins on the ESP32-A1S module. ES8388 slave-mode playback needs
 * MCLK; this matches the ADF board layer that previously drove MCLK on GPIO0.
 */
#define ESP_POCKET_AUDIO_I2S_PORT                I2S_NUM_0
#define ESP_POCKET_AUDIO_I2S_MCLK_GPIO           GPIO_NUM_0
#define ESP_POCKET_AUDIO_I2S_BCLK_GPIO           GPIO_NUM_27
#define ESP_POCKET_AUDIO_I2S_WS_GPIO             GPIO_NUM_25
#define ESP_POCKET_AUDIO_I2S_DOUT_GPIO           GPIO_NUM_26
#define ESP_POCKET_AUDIO_I2S_DIN_GPIO            GPIO_NUM_35
#define ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ      48000
#define ESP_POCKET_AUDIO_I2S_BITS_PER_SAMPLE     16

/* Optional external PA enable pin. Set once this signal is confirmed. */
#define ESP_POCKET_AUDIO_PA_ENABLE_GPIO          GPIO_NUM_NC

/* Dedicated control I2C bus for ADS1115 and PCF8574. */
#define ESP_POCKET_AUDIO_INPUT_I2C_PORT          I2C_NUM_1
#define ESP_POCKET_AUDIO_INPUT_I2C_SDA_GPIO      GPIO_NUM_21
#define ESP_POCKET_AUDIO_INPUT_I2C_SCL_GPIO      GPIO_NUM_22
#define ESP_POCKET_AUDIO_INPUT_I2C_CLK_HZ        100000
#define ESP_POCKET_AUDIO_INPUT_I2C_TIMEOUT_MS    50

/* 1.3 inch 128x64 SH1106 I2C OLED on the shared control I2C bus. */
#define ESP_POCKET_AUDIO_OLED_ENABLED            1
#define ESP_POCKET_AUDIO_OLED_ADDR               0x3C
#define ESP_POCKET_AUDIO_OLED_I2C_CLK_HZ         100000

/* ADS1115 analog input configuration. */
#define ESP_POCKET_AUDIO_ADS1115_ADDR            0x48
#define ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_CH   0
#define ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_CH   1
#define ESP_POCKET_AUDIO_ADS1115_DEADZONE_RAW    1200
#define ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW    0
#define ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW 16384
#define ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW    32767

/* PCF8574A switch expander on the shared control I2C bus. Inputs are quasi-bidirectional and active-low. */
#define ESP_POCKET_AUDIO_PCF8574_COUNT           1
#define ESP_POCKET_AUDIO_PCF8574_0_ADDR          0x38
#define ESP_POCKET_AUDIO_SWITCH_ACTIVE_LOW       1
#define ESP_POCKET_AUDIO_SWITCH_DEBOUNCE_MS      25

/* Logical switch mapping. Use expander index 0..1 and pin 0..7. */
#define ESP_POCKET_AUDIO_SW_CHORD_1_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_1_PIN          0
#define ESP_POCKET_AUDIO_SW_CHORD_2_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_2_PIN          1
#define ESP_POCKET_AUDIO_SW_CHORD_3_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_3_PIN          2
#define ESP_POCKET_AUDIO_SW_CHORD_4_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_4_PIN          3
#define ESP_POCKET_AUDIO_SW_CHORD_5_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_5_PIN          4
#define ESP_POCKET_AUDIO_SW_CHORD_6_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_6_PIN          5
#define ESP_POCKET_AUDIO_SW_CHORD_7_EXPANDER     0
#define ESP_POCKET_AUDIO_SW_CHORD_7_PIN          6
#define ESP_POCKET_AUDIO_SW_KEY_TEMPO_EXPANDER   0
#define ESP_POCKET_AUDIO_SW_KEY_TEMPO_PIN        7

/* Direct GPIO switch mapping. */
#define ESP_POCKET_AUDIO_SW_KEY_MODE_GPIO           GPIO_NUM_23
#define ESP_POCKET_AUDIO_SW_KEY_SOUND_GPIO          GPIO_NUM_18
#define ESP_POCKET_AUDIO_SW_JOYSTICK_CLICK_GPIO     GPIO_NUM_19
#define ESP_POCKET_AUDIO_PCF8574_INT_GPIO           GPIO_NUM_5

#ifdef __cplusplus
}
#endif

#endif
