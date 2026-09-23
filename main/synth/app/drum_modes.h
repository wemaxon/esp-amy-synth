#ifndef SYNTH_APP_DRUM_MODES_H
#define SYNTH_APP_DRUM_MODES_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "hmi/controls.h"
#include "synth/app/drum_transport.h"
#include "synth/drums.h"
#include "synth/play_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_DRUM_MODES_INITIALIZER \
    { \
        .loop = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER, \
    }

typedef struct {
    bool handled;
    bool ui_dirty;
    const char *status;
    const char *label;
} synth_app_drum_result_t;

typedef struct {
    synth_app_drum_transport_t loop;
    uint8_t auto_mask;
    synth_rhythm_rate_t auto_rate[SYNTH_DRUM_VOICE_COUNT];
    uint32_t auto_step_index[SYNTH_DRUM_VOICE_COUNT];
    TickType_t auto_next_tick[SYNTH_DRUM_VOICE_COUNT];
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
bool synth_app_drums_loop_active(const synth_app_drum_modes_t *drums);
const char *synth_app_drums_auto_rate_name(const synth_app_drum_modes_t *drums);

#ifdef __cplusplus
}
#endif

#endif
