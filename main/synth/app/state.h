#ifndef SYNTH_APP_STATE_H
#define SYNTH_APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "synth/app/chords.h"
#include "synth/app/drum_modes.h"
#include "synth/app/effects.h"
#include "synth/app/looper.h"
#include "synth/app/mixer.h"
#include "synth/app/modulation.h"
#include "synth/app/play_state.h"
#include "synth/app/sound.h"
#include "synth/app/tempo.h"
#include "synth/app/wheel.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"
#include "synth/mic_sample.h"

/*
 * Private aggregate owned by the application task.  Keeping this definition
 * out of the coordinator lets focused application modules consume explicit
 * state without introducing another singleton or widening the public API.
 */
typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_joystick_mode_t joystick_mode;
    synth_bass_mode_t bass_mode;
    bool voice_leading_enabled;
    synth_app_chord_state_t chords;
    synth_app_sound_t sound;
    synth_play_mode_t mode;
    synth_app_tempo_t tempo;
    synth_app_effects_t fx;
    synth_app_modulation_t lfo;
    synth_app_modulation_t tremolo;
    synth_app_wheel_t wheel;
    uint8_t mic_gain_percent;
    float mic_digital_gain;
    bool mic_record_button_down;
    TickType_t mic_last_ui_tick;
    synth_mic_sample_snapshot_t mic_sample;
    control_joystick_direction_t last_menu_direction;
    control_joystick_direction_t last_play_direction;
    control_joystick_direction_t joystick_direction;
    uint8_t slash_bass_slot;
    synth_app_looper_t looper;
    synth_app_mixer_t mixer;
    synth_app_drum_modes_t drums;
    /* Beat grid last sent to the looper (it aligns new recordings to it). */
    bool beat_grid_sent;
    uint64_t beat_grid_origin;
    uint16_t beat_grid_bpm;
    /* Beat last published to the audio-side scheduler. */
    synth_beat_config_t beat_published;
    bool mode_bpm_submenu;
    synth_app_play_state_t play;
    bool drone_active;
    uint8_t drone_slot;
    char chord_name[16];
    char status[22];
    TaskHandle_t task;
} synth_app_state_t;

#endif

