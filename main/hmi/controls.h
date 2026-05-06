#ifndef HMI_CONTROLS_H
#define HMI_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROL_KEY_F = 0,
    CONTROL_KEY_F_SHARP,
    CONTROL_KEY_G,
    CONTROL_KEY_G_SHARP,
    CONTROL_KEY_A,
    CONTROL_KEY_A_SHARP,
    CONTROL_KEY_H,
    CONTROL_JOYSTICK_CLICK,
    CONTROL_KEY_TEMPO,
    CONTROL_KEY_MODE,
    CONTROL_KEY_SOUND,
    CONTROL_SWITCH_COUNT,
} control_switch_t;

typedef enum {
    CONTROL_AXIS_X = 0,
    CONTROL_AXIS_Y,
    CONTROL_AXIS_COUNT,
} control_axis_t;

typedef enum {
    CONTROL_JOYSTICK_CENTER = 0,
    CONTROL_JOYSTICK_UP,
    CONTROL_JOYSTICK_UP_RIGHT,
    CONTROL_JOYSTICK_RIGHT,
    CONTROL_JOYSTICK_DOWN_RIGHT,
    CONTROL_JOYSTICK_DOWN,
    CONTROL_JOYSTICK_DOWN_LEFT,
    CONTROL_JOYSTICK_LEFT,
    CONTROL_JOYSTICK_UP_LEFT,
} control_joystick_direction_t;

typedef struct {
    int16_t raw;
    float value;
} control_axis_state_t;

typedef struct {
    control_axis_state_t x;
    control_axis_state_t y;
    control_joystick_direction_t direction;
} control_joystick_state_t;

typedef enum {
    CONTROL_EVENT_SWITCH_PRESSED = 0,
    CONTROL_EVENT_SWITCH_RELEASED,
} control_event_type_t;

typedef struct {
    control_event_type_t type;
    control_switch_t sw;
} control_event_t;

esp_err_t controls_init(i2c_master_bus_handle_t bus);
esp_err_t controls_update(void);

bool controls_switch_is_pressed(control_switch_t sw);
uint32_t controls_pressed_mask(void);
esp_err_t controls_pop_event(control_event_t *event);

esp_err_t controls_axis_read(control_axis_t axis, control_axis_state_t *state);
esp_err_t controls_joystick_read(control_joystick_state_t *state);

bool controls_expander_interrupt_asserted(void);
gpio_num_t controls_expander_interrupt_gpio(void);

control_joystick_direction_t controls_joystick_direction_from_axes(float x, float y);
const char *controls_joystick_direction_name(control_joystick_direction_t direction);
const char *controls_switch_name(control_switch_t sw);
const char *controls_switch_short_name(control_switch_t sw);

#ifdef __cplusplus
}
#endif

#endif
