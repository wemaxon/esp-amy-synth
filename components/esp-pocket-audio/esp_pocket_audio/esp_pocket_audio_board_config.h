#ifndef ESP_POCKET_AUDIO_BOARD_CONFIG_H
#define ESP_POCKET_AUDIO_BOARD_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Edit this file when the hardware wiring is finalized.
 *
 * The audio codec keeps the ESP32-A1S / ES8388 wiring. The ADS1115 joystick and
 * PCF8574 switches use a separate I2C controller so the controls do not share
 * the codec control bus.
 */

/* AI Thinker ESP32-A1S / ES8388 variant. Set to 5 or 7. */
#define ESP_POCKET_AUDIO_A1S_VARIANT             5

/* Dedicated control I2C bus for ADS1115 and PCF8574. */
#define ESP_POCKET_AUDIO_INPUT_I2C_PORT          I2C_NUM_1
#define ESP_POCKET_AUDIO_INPUT_I2C_SDA_GPIO      GPIO_NUM_16
#define ESP_POCKET_AUDIO_INPUT_I2C_SCL_GPIO      GPIO_NUM_17
#define ESP_POCKET_AUDIO_INPUT_I2C_CLK_HZ        100000
#define ESP_POCKET_AUDIO_INPUT_I2C_TIMEOUT_MS    50

/* ADS1115 analog input configuration. */
#define ESP_POCKET_AUDIO_ADS1115_ADDR            0x48
#define ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_CH   0
#define ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_CH   1
#define ESP_POCKET_AUDIO_ADS1115_DEADZONE_RAW    1200
#define ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW    0
#define ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW 16384
#define ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW    32767

/*
 * PCF8574 switch expanders. PCF8574 inputs are quasi-bidirectional; the driver
 * writes 1s to all pins before reading them. Switches are active-low.
 *
 * One PCF8574 gives 8 switch inputs. Enable the second expander when more
 * inputs are wired.
 */
#define ESP_POCKET_AUDIO_PCF8574_COUNT           1
#define ESP_POCKET_AUDIO_PCF8574_0_ADDR          0x20
#define ESP_POCKET_AUDIO_PCF8574_1_ADDR          0x21
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
#define ESP_POCKET_AUDIO_SW_JOYSTICK_CLICK_EXPANDER 0
#define ESP_POCKET_AUDIO_SW_JOYSTICK_CLICK_PIN   7

/* Defaults assume a second PCF8574 is added for menu buttons later. */
#define ESP_POCKET_AUDIO_SW_KEY_MENU_EXPANDER    1
#define ESP_POCKET_AUDIO_SW_KEY_MENU_PIN         0
#define ESP_POCKET_AUDIO_SW_TEMPO_MENU_EXPANDER  1
#define ESP_POCKET_AUDIO_SW_TEMPO_MENU_PIN       1
#define ESP_POCKET_AUDIO_SW_SOUND_MENU_EXPANDER  1
#define ESP_POCKET_AUDIO_SW_SOUND_MENU_PIN       2

/*
 * Optional PCF8574 interrupt pin. Set to GPIO_NUM_NC if not connected.
 * The input API can poll without this pin, but wiring INT lets higher layers
 * avoid unnecessary reads later.
 */
#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC ((gpio_num_t)-1)
#endif
#define ESP_POCKET_AUDIO_PCF8574_INT_GPIO        GPIO_NUM_34

#ifdef __cplusplus
}
#endif

#endif
