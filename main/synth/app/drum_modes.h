#ifndef SYNTH_APP_DRUM_MODES_H
#define SYNTH_APP_DRUM_MODES_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "hmi/controls.h"
#include "synth/drums.h"
#include "synth/looper.h"
#include "synth/play_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_DRUM_NO_BOUNCE_TRACK UINT8_MAX

#define SYNTH_APP_DRUM_MODES_INITIALIZER \
    { \
        .kit = SYNTH_DRUM_KIT_TIGHT, \
        .loop_style = SYNTH_DRUM_LOOP_ROCK, \
        .loop_variation = SYNTH_DRUM_LOOP_VARIATION_ORIGINAL, \
        .bounce_request_id = 0, \
        .bounce_track = SYNTH_APP_DRUM_NO_BOUNCE_TRACK, \
        .bounce_style = SYNTH_DRUM_LOOP_ROCK, \
        .owned_track = SYNTH_APP_DRUM_NO_BOUNCE_TRACK, \
        .owned_style = SYNTH_DRUM_LOOP_ROCK, \
    }

typedef struct {
    bool handled;
    bool ui_dirty;
    bool clear_track;
    uint8_t action_track;
    const char *status;
    const char *label;
} synth_app_drum_result_t;

typedef struct {
    synth_drum_kit_t kit;
    synth_drum_loop_style_t loop_style;
    synth_drum_loop_variation_t loop_variation;
    bool loop_running;
    uint32_t loop_step_index;
    TickType_t loop_next_tick;
    uint8_t auto_mask;
    synth_rhythm_rate_t auto_rate[SYNTH_DRUM_VOICE_COUNT];
    uint32_t auto_step_index[SYNTH_DRUM_VOICE_COUNT];
    TickType_t auto_next_tick[SYNTH_DRUM_VOICE_COUNT];
    synth_looper_request_id_t bounce_request_id;
    uint8_t bounce_track;
    synth_drum_loop_style_t bounce_style;
    uint8_t owned_track;
    synth_drum_loop_style_t owned_style;
} synth_app_drum_modes_t;

bool synth_app_drums_mode_uses_buttons(synth_play_mode_t mode);
synth_app_drum_result_t synth_app_drums_set_kit_for_button(synth_app_drum_modes_t *drums,
                                                           uint8_t button);
synth_app_drum_result_t synth_app_drums_button_press(synth_app_drum_modes_t *drums,
                                                     synth_play_mode_t mode,
                                                     uint8_t button,
                                                     control_joystick_direction_t direction,
                                                     uint16_t bpm,
                                                     TickType_t now);
synth_app_drum_result_t synth_app_drums_button_release(synth_app_drum_modes_t *drums,
                                                       synth_play_mode_t mode,
                                                       uint8_t button);
synth_app_drum_result_t synth_app_drums_joystick(synth_app_drum_modes_t *drums,
                                                 synth_play_mode_t mode,
                                                 uint8_t held_mask,
                                                 control_joystick_direction_t direction,
                                                 TickType_t now);
bool synth_app_drums_update(synth_app_drum_modes_t *drums, uint16_t bpm, TickType_t now);
void synth_app_drums_stop_auto(synth_app_drum_modes_t *drums);
void synth_app_drums_stop_loop(synth_app_drum_modes_t *drums);
void synth_app_drums_mark_bounce_request(synth_app_drum_modes_t *drums,
                                         synth_looper_request_id_t request_id);
bool synth_app_drums_resolve_bounce_request(synth_app_drum_modes_t *drums,
                                            synth_looper_request_id_t request_id,
                                            uint8_t track);
bool synth_app_drums_bounce_request_pending(const synth_app_drum_modes_t *drums);
bool synth_app_drums_bounce_pending(const synth_app_drum_modes_t *drums);
bool synth_app_drums_finish_bounce_if_complete(synth_app_drum_modes_t *drums,
                                               synth_looper_track_state_t track_state);
bool synth_app_drums_loop_active(const synth_app_drum_modes_t *drums);
const char *synth_app_drums_auto_rate_name(const synth_app_drum_modes_t *drums);

#ifdef __cplusplus
}
#endif

#endif
