#ifndef SYNTH_APP_CONTROL_ROUTE_H
#define SYNTH_APP_CONTROL_ROUTE_H

#include <stdbool.h>

#include "synth/play_mode.h"
#include "hmi/controls.h"

typedef enum {
    SYNTH_APP_CONTROL_ROUTE_PLAY = 0,
    SYNTH_APP_CONTROL_ROUTE_KEY_MENU,
    SYNTH_APP_CONTROL_ROUTE_SOUND_MENU,
    SYNTH_APP_CONTROL_ROUTE_MODE_MENU,
    SYNTH_APP_CONTROL_ROUTE_LOOPER,
    SYNTH_APP_CONTROL_ROUTE_MIXER,
    SYNTH_APP_CONTROL_ROUTE_DRUMS,
} synth_app_control_route_t;

typedef enum {
    SYNTH_APP_MENU_NONE,
    SYNTH_APP_MENU_KEY,
    SYNTH_APP_MENU_GLOBAL_OCTAVE,
    SYNTH_APP_MENU_BUTTON_OCTAVE,
    SYNTH_APP_MENU_SOUND,
    SYNTH_APP_MENU_EFFECT,
    SYNTH_APP_MENU_MODE,
    SYNTH_APP_MENU_BPM,
    SYNTH_APP_MENU_ENTER_BPM,
    SYNTH_APP_MENU_LEAVE_BPM,
    SYNTH_APP_MENU_CYCLE_PARAM,
} synth_app_menu_action_t;

typedef struct {
    synth_app_menu_action_t action;
    int8_t steps;
} synth_app_menu_result_t;

/* Menu direction changes are edge-triggered, shared across the three menus. */
synth_app_menu_result_t synth_app_menu_update(synth_app_control_route_t route,
                                              control_joystick_direction_t direction,
                                              control_joystick_direction_t *previous,
                                              bool chord_held,
                                              bool *bpm_submenu);

synth_app_control_route_t synth_app_control_route(bool key_menu_pressed,
                                                  bool sound_menu_pressed,
                                                  bool mode_menu_pressed,
                                                  bool looper_visible,
                                                  synth_play_mode_t mode,
                                                  bool chord_held);

#endif
