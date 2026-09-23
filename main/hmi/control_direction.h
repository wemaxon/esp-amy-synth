#ifndef HMI_CONTROL_DIRECTION_H
#define HMI_CONTROL_DIRECTION_H

/* Value-only input vocabulary; no driver or platform dependencies. */
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

#endif
