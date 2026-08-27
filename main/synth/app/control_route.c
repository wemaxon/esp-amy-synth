#include "synth/app/control_route.h"

synth_app_control_route_t synth_app_control_route(bool key_menu_pressed,
                                                  bool sound_menu_pressed,
                                                  bool mode_menu_pressed,
                                                  bool looper_visible,
                                                  synth_play_mode_t mode,
                                                  bool chord_held)
{
    if (key_menu_pressed) {
        return SYNTH_APP_CONTROL_ROUTE_KEY_MENU;
    }
    if (sound_menu_pressed) {
        return SYNTH_APP_CONTROL_ROUTE_SOUND_MENU;
    }
    if (mode_menu_pressed) {
        return SYNTH_APP_CONTROL_ROUTE_MODE_MENU;
    }
    if (looper_visible && !chord_held) {
        return SYNTH_APP_CONTROL_ROUTE_LOOPER;
    }
    if (mode == SYNTH_PLAY_MODE_MIXER) {
        return SYNTH_APP_CONTROL_ROUTE_MIXER;
    }
    if (mode == SYNTH_PLAY_MODE_DRUM_MODE ||
        mode == SYNTH_PLAY_MODE_DRUM_LOOPS ||
        mode == SYNTH_PLAY_MODE_AUTO_DRUM) {
        return SYNTH_APP_CONTROL_ROUTE_DRUMS;
    }
    return SYNTH_APP_CONTROL_ROUTE_PLAY;
}
