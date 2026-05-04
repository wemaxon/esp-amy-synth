#ifndef ESP_POCKET_AUDIO_BOARD_DEF_H
#define ESP_POCKET_AUDIO_BOARD_DEF_H

#include "audio_hal.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_pocket_audio_board_config.h"
#include "esp_pocket_audio_input.h"

/**
 * @brief SD card function definition
 *
 * esp-pocket-audio keeps SD disabled by default. Enable and fill pins here if the
 * hardware design later needs SD card support.
 */
#define FUNC_SDCARD_EN            (0)
#define SDCARD_OPEN_FILE_NUM_MAX  5
#define SDCARD_INTR_GPIO          -1
#define ESP_SD_PIN_CLK            -1
#define ESP_SD_PIN_CMD            -1
#define ESP_SD_PIN_D0             -1
#define ESP_SD_PIN_D1             -1
#define ESP_SD_PIN_D2             -1
#define ESP_SD_PIN_D3             -1

/**
 * @brief LED function definition
 */
#define FUNC_SYS_LEN_EN           (1)
#define GREEN_LED_GPIO            GPIO_NUM_22

/**
 * @brief Audio codec chip function definition
 */
#define FUNC_AUDIO_CODEC_EN       (1)
#define AUXIN_DETECT_GPIO         (-1)
#define HEADPHONE_DETECT          GPIO_NUM_39
#define PA_ENABLE_GPIO            GPIO_NUM_21
#define CODEC_ADC_I2S_PORT        (0)
#define CODEC_ADC_BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT
#define CODEC_ADC_SAMPLE_RATE     (48000)
#define RECORD_HARDWARE_AEC       (false)
#define BOARD_PA_GAIN             (10)

extern audio_hal_func_t AUDIO_CODEC_ES8388_DEFAULT_HANDLE;
#define AUDIO_CODEC_DEFAULT_CONFIG(){                   \
        .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,         \
        .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,        \
        .i2s_iface = {                                  \
            .mode = AUDIO_HAL_MODE_SLAVE,               \
            .fmt = AUDIO_HAL_I2S_NORMAL,                \
            .samples = AUDIO_HAL_48K_SAMPLES,           \
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,        \
        },                                              \
};

/**
 * @brief Button function definition
 *
 * These IDs intentionally use logical switch IDs from esp_pocket_audio_input.h.
 * They are not ESP32 GPIO numbers.
 */
#define FUNC_BUTTON_EN            (1)
#define INPUT_KEY_NUM             11

#define BUTTON_CHORD_1_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_1
#define BUTTON_CHORD_2_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_2
#define BUTTON_CHORD_3_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_3
#define BUTTON_CHORD_4_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_4
#define BUTTON_CHORD_5_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_5
#define BUTTON_CHORD_6_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_6
#define BUTTON_CHORD_7_ID         ESP_POCKET_AUDIO_SWITCH_CHORD_7
#define BUTTON_JOYSTICK_CLICK_ID  ESP_POCKET_AUDIO_SWITCH_JOYSTICK_CLICK
#define BUTTON_KEY_MENU_ID        ESP_POCKET_AUDIO_SWITCH_KEY_MENU
#define BUTTON_TEMPO_MENU_ID      ESP_POCKET_AUDIO_SWITCH_TEMPO_MENU
#define BUTTON_SOUND_MENU_ID      ESP_POCKET_AUDIO_SWITCH_SOUND_MENU

/* Compatibility aliases for current app_synth.c while the app is still using
 * the ADF demo button names. Replace app code with logical names over time.
 */
#define BUTTON_REC_ID             BUTTON_CHORD_1_ID
#define BUTTON_MODE_ID            BUTTON_CHORD_2_ID
#define BUTTON_SET_ID             BUTTON_CHORD_3_ID
#define BUTTON_PLAY_ID            BUTTON_CHORD_4_ID
#define BUTTON_VOLDOWN_ID         BUTTON_CHORD_5_ID
#define BUTTON_VOLUP_ID           BUTTON_CHORD_6_ID

#define INPUT_KEY_DEFAULT_INFO() { }

#endif
