#ifndef SYNTH_APP_MIXER_H
#define SYNTH_APP_MIXER_H

#include <stdbool.h>
#include <stdint.h>

#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_MIXER_NO_TRACK UINT8_MAX
#define SYNTH_APP_MIXER_INITIALIZER \
    { \
        .wheel_track = SYNTH_APP_MIXER_NO_TRACK, \
        .wheel_start_percent = 100, \
    }

typedef enum {
    SYNTH_APP_MIXER_ACTION_NONE = 0,
    SYNTH_APP_MIXER_ACTION_TOGGLE_METRONOME,
    SYNTH_APP_MIXER_ACTION_TOGGLE_MUTE,
    SYNTH_APP_MIXER_ACTION_TOGGLE_SOLO,
    SYNTH_APP_MIXER_ACTION_TOGGLE_PAUSE,
} synth_app_mixer_action_t;

typedef struct {
    uint8_t held_mask;
    uint8_t action_mask;
    uint8_t wheel_track;
    uint8_t wheel_start_percent;
} synth_app_mixer_t;

typedef struct {
    bool handled;
    bool ui_dirty;
    synth_app_mixer_action_t action;
    uint8_t action_track;
    uint8_t focus_track;
    const char *status;
} synth_app_mixer_result_t;

void synth_app_mixer_reset(synth_app_mixer_t *mixer);
uint8_t synth_app_mixer_first_held_track(const synth_app_mixer_t *mixer);
void synth_app_mixer_mark_action(synth_app_mixer_t *mixer, uint8_t track);
bool synth_app_mixer_wheel_moved(synth_app_mixer_t *mixer,
                                 uint8_t track,
                                 uint8_t percent,
                                 uint8_t threshold_percent);
synth_app_mixer_result_t synth_app_mixer_handle_chord_event(synth_app_mixer_t *mixer,
                                                            const control_event_t *event,
                                                            uint8_t degree,
                                                            uint8_t wheel_percent);
synth_app_mixer_result_t synth_app_mixer_handle_joystick_click(synth_app_mixer_t *mixer);

#ifdef __cplusplus
}
#endif

#endif
