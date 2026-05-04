#ifndef MY_SYNTH_V1_INPUT_H
#define MY_SYNTH_V1_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MY_SYNTH_V1_SWITCH_CHORD_1 = 0,
    MY_SYNTH_V1_SWITCH_CHORD_2,
    MY_SYNTH_V1_SWITCH_CHORD_3,
    MY_SYNTH_V1_SWITCH_CHORD_4,
    MY_SYNTH_V1_SWITCH_CHORD_5,
    MY_SYNTH_V1_SWITCH_CHORD_6,
    MY_SYNTH_V1_SWITCH_CHORD_7,
    MY_SYNTH_V1_SWITCH_JOYSTICK_CLICK,
    MY_SYNTH_V1_SWITCH_KEY_MENU,
    MY_SYNTH_V1_SWITCH_TEMPO_MENU,
    MY_SYNTH_V1_SWITCH_SOUND_MENU,
    MY_SYNTH_V1_SWITCH_COUNT,
} my_synth_v1_switch_t;

typedef enum {
    MY_SYNTH_V1_AXIS_X = 0,
    MY_SYNTH_V1_AXIS_Y,
    MY_SYNTH_V1_AXIS_COUNT,
} my_synth_v1_axis_t;

typedef struct {
    int16_t raw;
    float value;
} my_synth_v1_axis_state_t;

typedef struct {
    my_synth_v1_axis_state_t x;
    my_synth_v1_axis_state_t y;
} my_synth_v1_joystick_state_t;

typedef enum {
    MY_SYNTH_V1_INPUT_EVENT_SWITCH_PRESSED = 0,
    MY_SYNTH_V1_INPUT_EVENT_SWITCH_RELEASED,
} my_synth_v1_input_event_type_t;

typedef struct {
    my_synth_v1_input_event_type_t type;
    my_synth_v1_switch_t sw;
} my_synth_v1_input_event_t;

esp_err_t my_synth_v1_input_init(void);
esp_err_t my_synth_v1_input_update(void);
bool my_synth_v1_input_int_is_asserted(void);
gpio_num_t my_synth_v1_input_int_gpio(void);

bool my_synth_v1_switch_is_pressed(my_synth_v1_switch_t sw);
uint32_t my_synth_v1_switch_pressed_mask(void);
esp_err_t my_synth_v1_input_pop_event(my_synth_v1_input_event_t *event);

esp_err_t my_synth_v1_axis_read(my_synth_v1_axis_t axis, my_synth_v1_axis_state_t *state);
esp_err_t my_synth_v1_joystick_read(my_synth_v1_joystick_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
