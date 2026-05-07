#ifndef HMI_PLAY_UI_H
#define HMI_PLAY_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"
#include "hmi/controls.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAY_UI_SCREEN_HOME = 0,
    PLAY_UI_SCREEN_KEY,
    PLAY_UI_SCREEN_SOUND,
    PLAY_UI_SCREEN_MODE,
    PLAY_UI_SCREEN_STATUS,
} play_ui_screen_t;

typedef enum {
    PLAY_UI_EFFECT_REVERB = 0,
    PLAY_UI_EFFECT_DELAY,
    PLAY_UI_EFFECT_CHORUS,
    PLAY_UI_EFFECT_FILTER,
    PLAY_UI_EFFECT_TREMOLO,
    PLAY_UI_EFFECT_FLANGER,
    PLAY_UI_EFFECT_LFO,
    PLAY_UI_EFFECT_JOYSTICK,
    PLAY_UI_EFFECT_COUNT,
} play_ui_effect_t;

typedef struct {
    play_ui_screen_t screen;
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_joystick_mode_t joystick_mode;
    synth_sound_t sound;
    synth_play_mode_t mode;
    uint16_t bpm;
    uint8_t held_mask;
    uint8_t focus_slot;
    uint8_t effect_index;
    bool effects[PLAY_UI_EFFECT_COUNT];
    uint8_t lfo_level;
    uint8_t delay_level;
    uint8_t tremolo_level;
    bool wheel_present;
    uint8_t wheel_percent;
    control_joystick_direction_t joystick_direction;
    char chord_name[16];
    char status[22];
} play_ui_model_t;

esp_err_t play_ui_start(const esp_pocket_audio_hw_t *hw);
void play_ui_model_init(play_ui_model_t *model);
void play_ui_update(const play_ui_model_t *model);
void play_ui_show_status(const char *status);
const char *play_ui_effect_name(play_ui_effect_t effect);

#ifdef __cplusplus
}
#endif

#endif
