#include "synth/app/control_route.c"
#include "synth/app/sound.c"

int32_t host_vector_menu_direction_edges(void)
{
    control_joystick_direction_t previous = CONTROL_JOYSTICK_CENTER;
    bool bpm = false;
    synth_app_menu_result_t r = synth_app_menu_update(
        SYNTH_APP_CONTROL_ROUTE_KEY_MENU, CONTROL_JOYSTICK_UP, &previous, true, &bpm);
    if (r.action != SYNTH_APP_MENU_BUTTON_OCTAVE || r.steps != 1) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_KEY_MENU, CONTROL_JOYSTICK_UP, &previous, false, &bpm);
    if (r.action != SYNTH_APP_MENU_NONE) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_KEY_MENU, CONTROL_JOYSTICK_DOWN, &previous, false, &bpm);
    return r.action == SYNTH_APP_MENU_GLOBAL_OCTAVE && r.steps == -1;
}

int32_t host_vector_menu_sound_and_bpm(void)
{
    control_joystick_direction_t previous = CONTROL_JOYSTICK_CENTER;
    bool bpm = false;
    synth_app_menu_result_t r = synth_app_menu_update(
        SYNTH_APP_CONTROL_ROUTE_SOUND_MENU, CONTROL_JOYSTICK_UP, &previous, false, &bpm);
    if (r.action != SYNTH_APP_MENU_EFFECT || r.steps != -1) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_SOUND_MENU, CONTROL_JOYSTICK_RIGHT, &previous, false, &bpm);
    if (r.action != SYNTH_APP_MENU_SOUND || r.steps != 1) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_MODE_MENU, CONTROL_JOYSTICK_UP, &previous, false, &bpm);
    if (!bpm || r.action != SYNTH_APP_MENU_ENTER_BPM) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_MODE_MENU, CONTROL_JOYSTICK_LEFT, &previous, false, &bpm);
    if (r.action != SYNTH_APP_MENU_BPM || r.steps != -1) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_MODE_MENU, CONTROL_JOYSTICK_DOWN, &previous, false, &bpm);
    if (bpm || r.action != SYNTH_APP_MENU_LEAVE_BPM) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_MODE_MENU, CONTROL_JOYSTICK_CENTER, &previous, false, &bpm);
    if (r.action != SYNTH_APP_MENU_NONE) return 0;
    r = synth_app_menu_update(SYNTH_APP_CONTROL_ROUTE_MODE_MENU, CONTROL_JOYSTICK_DOWN, &previous, false, &bpm);
    return r.action == SYNTH_APP_MENU_CYCLE_PARAM;
}

int32_t host_vector_menu_priority(void)
{
    return synth_app_control_route(true, true, true, true, SYNTH_PLAY_MODE_MIXER, true) == SYNTH_APP_CONTROL_ROUTE_KEY_MENU &&
           synth_app_control_route(false, true, true, true, SYNTH_PLAY_MODE_MIXER, true) == SYNTH_APP_CONTROL_ROUTE_SOUND_MENU &&
           synth_app_control_route(false, false, true, true, SYNTH_PLAY_MODE_MIXER, true) == SYNTH_APP_CONTROL_ROUTE_MODE_MENU;
}

int32_t host_vector_sound_browse_wrap(void)
{
    synth_app_sound_t sound = SYNTH_APP_SOUND_INITIALIZER(SYNTH_SOUND_SAW);
    synth_app_sound_schedule(&sound, SYNTH_SOUND_SAW, UINT32_MAX - 99U, 180);
    if (synth_app_sound_pending_due(&sound, 79) || !synth_app_sound_pending_due(&sound, 80)) return 0;
    synth_app_sound_mark_applied(&sound, SYNTH_SOUND_SAW);
    return !synth_app_sound_pending_due(&sound, 999);
}
