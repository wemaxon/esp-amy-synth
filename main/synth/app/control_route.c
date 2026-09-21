#include "synth/app/control_route.h"

synth_app_menu_result_t synth_app_menu_update(synth_app_control_route_t route,
                                              control_joystick_direction_t direction,
                                              control_joystick_direction_t *previous,
                                              bool chord_held,
                                              bool *bpm_submenu)
{
    synth_app_menu_result_t result = {0};
    if (direction == *previous) {
        return result;
    }
    *previous = direction;

    bool horizontal = direction == CONTROL_JOYSTICK_LEFT || direction == CONTROL_JOYSTICK_RIGHT;
    bool vertical = direction == CONTROL_JOYSTICK_UP || direction == CONTROL_JOYSTICK_DOWN;
    if (!horizontal && !vertical) {
        return result;
    }
    result.steps = (direction == CONTROL_JOYSTICK_RIGHT || direction == CONTROL_JOYSTICK_UP) ? 1 : -1;
    switch (route) {
    case SYNTH_APP_CONTROL_ROUTE_KEY_MENU:
        result.action = horizontal ? SYNTH_APP_MENU_KEY :
            (chord_held ? SYNTH_APP_MENU_BUTTON_OCTAVE : SYNTH_APP_MENU_GLOBAL_OCTAVE);
        break;
    case SYNTH_APP_CONTROL_ROUTE_SOUND_MENU:
        result.action = horizontal ? SYNTH_APP_MENU_SOUND : SYNTH_APP_MENU_EFFECT;
        if (vertical) {
            result.steps = -result.steps;
        }
        break;
    case SYNTH_APP_CONTROL_ROUTE_MODE_MENU:
        if (*bpm_submenu) {
            result.action = horizontal ? SYNTH_APP_MENU_BPM : SYNTH_APP_MENU_LEAVE_BPM;
            if (vertical) {
                *bpm_submenu = false;
            }
        } else if (horizontal) {
            result.action = SYNTH_APP_MENU_MODE;
        } else if (direction == CONTROL_JOYSTICK_UP) {
            *bpm_submenu = true;
            result.action = SYNTH_APP_MENU_ENTER_BPM;
        } else {
            result.action = SYNTH_APP_MENU_CYCLE_PARAM;
        }
        break;
    default:
        break;
    }
    return result;
}

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
