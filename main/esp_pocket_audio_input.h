#ifndef ESP_POCKET_AUDIO_INPUT_H
#define ESP_POCKET_AUDIO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_pocket_audio_joystick.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_POCKET_AUDIO_SWITCH_CHORD_1 = 0,
    ESP_POCKET_AUDIO_SWITCH_CHORD_2,
    ESP_POCKET_AUDIO_SWITCH_CHORD_3,
    ESP_POCKET_AUDIO_SWITCH_CHORD_4,
    ESP_POCKET_AUDIO_SWITCH_CHORD_5,
    ESP_POCKET_AUDIO_SWITCH_CHORD_6,
    ESP_POCKET_AUDIO_SWITCH_CHORD_7,
    ESP_POCKET_AUDIO_SWITCH_JOYSTICK_CLICK,
    ESP_POCKET_AUDIO_SWITCH_KEY_TEMPO,
    ESP_POCKET_AUDIO_SWITCH_KEY_MODE,
    ESP_POCKET_AUDIO_SWITCH_KEY_SOUND,
    ESP_POCKET_AUDIO_SWITCH_COUNT,
} esp_pocket_audio_switch_t;

typedef enum {
    ESP_POCKET_AUDIO_AXIS_X = 0,
    ESP_POCKET_AUDIO_AXIS_Y,
    ESP_POCKET_AUDIO_AXIS_COUNT,
} esp_pocket_audio_axis_t;

typedef struct {
    int16_t raw;
    float value;
} esp_pocket_audio_axis_state_t;

typedef struct {
    esp_pocket_audio_axis_state_t x;
    esp_pocket_audio_axis_state_t y;
    esp_pocket_audio_joystick_direction_t direction;
} esp_pocket_audio_joystick_state_t;

typedef enum {
    ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_PRESSED = 0,
    ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_RELEASED,
} esp_pocket_audio_input_event_type_t;

typedef struct {
    esp_pocket_audio_input_event_type_t type;
    esp_pocket_audio_switch_t sw;
} esp_pocket_audio_input_event_t;

esp_err_t esp_pocket_audio_input_init(i2c_master_bus_handle_t bus);
esp_err_t esp_pocket_audio_input_update(void);
bool esp_pocket_audio_input_int_is_asserted(void);
gpio_num_t esp_pocket_audio_input_int_gpio(void);

bool esp_pocket_audio_switch_is_pressed(esp_pocket_audio_switch_t sw);
uint32_t esp_pocket_audio_switch_pressed_mask(void);
esp_err_t esp_pocket_audio_input_pop_event(esp_pocket_audio_input_event_t *event);

esp_err_t esp_pocket_audio_axis_read(esp_pocket_audio_axis_t axis, esp_pocket_audio_axis_state_t *state);
esp_err_t esp_pocket_audio_joystick_read(esp_pocket_audio_joystick_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
