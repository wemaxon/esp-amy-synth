#ifndef SYNTH_APP_CONTROL_ROUTE_H
#define SYNTH_APP_CONTROL_ROUTE_H

#include <stdbool.h>

#include "synth/play_mode.h"

typedef enum {
    SYNTH_APP_CONTROL_ROUTE_PLAY = 0,
    SYNTH_APP_CONTROL_ROUTE_KEY_MENU,
    SYNTH_APP_CONTROL_ROUTE_SOUND_MENU,
    SYNTH_APP_CONTROL_ROUTE_MODE_MENU,
    SYNTH_APP_CONTROL_ROUTE_LOOPER,
    SYNTH_APP_CONTROL_ROUTE_MIXER,
    SYNTH_APP_CONTROL_ROUTE_DRUMS,
} synth_app_control_route_t;

synth_app_control_route_t synth_app_control_route(bool key_menu_pressed,
                                                  bool sound_menu_pressed,
                                                  bool mode_menu_pressed,
                                                  bool looper_visible,
                                                  synth_play_mode_t mode,
                                                  bool chord_held);

#endif
