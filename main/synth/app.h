#ifndef SYNTH_APP_H
#define SYNTH_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"
#include "hmi/control_direction.h"
#include "hmi/play_ui.h"
#include "synth/beat_scheduler.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A consistent copy of the application state, taken on the application task. */
typedef struct {
    synth_play_mode_t mode;
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave;
    int8_t button_octave[SYNTH_CHORD_BUTTON_COUNT];
    uint8_t held_mask;
    uint8_t focus_slot;
    char chord_name[16];
    bool focused_chord_valid; /* the focused button is held with a chord */
    synth_chord_t focused_chord;
    synth_chord_modifier_t modifier;
    control_joystick_direction_t joystick_direction;
    synth_joystick_mode_t joystick_mode;
    play_ui_effect_t selected_setting;
    bool voice_leading;
    synth_bass_mode_t bass_mode;
    synth_sound_t sound;
    uint16_t bpm;
    char status[22];
    synth_beat_config_t beat;
    uint32_t stack_free_bytes;
} synth_app_snapshot_t;

esp_err_t synth_app_start(const esp_pocket_audio_hw_t *hw);
/* Fills snapshot within one application update (20 ms). One caller at a time. */
esp_err_t synth_app_snapshot(synth_app_snapshot_t *snapshot, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
