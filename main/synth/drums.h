#ifndef SYNTH_DRUMS_H
#define SYNTH_DRUMS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_DRUM_BUTTON_COUNT 7
#define SYNTH_DRUM_PATTERN_STEPS 16

typedef enum {
    SYNTH_DRUM_VOICE_KICK = 0,
    SYNTH_DRUM_VOICE_KICK_ALT,
    SYNTH_DRUM_VOICE_SNARE,
    SYNTH_DRUM_VOICE_CLOSED_HAT,
    SYNTH_DRUM_VOICE_TOM,
    SYNTH_DRUM_VOICE_BELL,
    SYNTH_DRUM_VOICE_OPEN_HAT,
    SYNTH_DRUM_VOICE_COUNT,
} synth_drum_voice_t;

typedef enum {
    SYNTH_DRUM_KIT_TIGHT = 0,
    SYNTH_DRUM_KIT_X0X,
    SYNTH_DRUM_KIT_X9X,
    SYNTH_DRUM_KIT_LYNN,
    SYNTH_DRUM_KIT_KR78,
    SYNTH_DRUM_KIT_TRAP,
    SYNTH_DRUM_KIT_COUNT,
} synth_drum_kit_t;

typedef enum {
    SYNTH_DRUM_LOOP_ROCK = 0,
    SYNTH_DRUM_LOOP_DISCO,
    SYNTH_DRUM_LOOP_REGGAE,
    SYNTH_DRUM_LOOP_FUNK,
    SYNTH_DRUM_LOOP_HIP_HOP,
    SYNTH_DRUM_LOOP_ELECTRO,
    SYNTH_DRUM_LOOP_JAZZ,
    SYNTH_DRUM_LOOP_STYLE_COUNT,
} synth_drum_loop_style_t;

typedef enum {
    SYNTH_DRUM_LOOP_VARIATION_ORIGINAL = 0,
    SYNTH_DRUM_LOOP_VARIATION_GHOSTS,
    SYNTH_DRUM_LOOP_VARIATION_BUSY_HATS,
    SYNTH_DRUM_LOOP_VARIATION_SYNC_KICKS,
    SYNTH_DRUM_LOOP_VARIATION_FILLS,
    SYNTH_DRUM_LOOP_VARIATION_HALF_TIME,
    SYNTH_DRUM_LOOP_VARIATION_DOUBLE_TIME,
    SYNTH_DRUM_LOOP_VARIATION_JAZZ,
    SYNTH_DRUM_LOOP_VARIATION_COUNT,
} synth_drum_loop_variation_t;

typedef struct {
    uint8_t mask;
    uint8_t accent_mask;
} synth_drum_pattern_step_t;

const char *synth_drum_voice_name(synth_drum_voice_t voice);
const char *synth_drum_kit_name(synth_drum_kit_t kit);
const char *synth_drum_loop_style_name(synth_drum_loop_style_t style);
const char *synth_drum_loop_variation_name(synth_drum_loop_variation_t variation);
const char *synth_drum_loop_variation_short_name(synth_drum_loop_variation_t variation);

synth_drum_kit_t synth_drum_kit_for_button(uint8_t button);
synth_drum_kit_t synth_drum_kit_step(synth_drum_kit_t kit, int8_t steps);
synth_drum_voice_t synth_drum_voice_for_button(uint8_t button);
synth_drum_loop_style_t synth_drum_loop_style_for_button(uint8_t button);
synth_drum_loop_variation_t synth_drum_loop_variation_step(synth_drum_loop_variation_t variation,
                                                           int8_t steps);

synth_drum_pattern_step_t synth_drum_pattern_step(synth_drum_loop_style_t style,
                                                  synth_drum_loop_variation_t variation,
                                                  uint8_t step);
esp_err_t synth_drums_hit(synth_drum_kit_t kit, synth_drum_voice_t voice, uint8_t velocity);
uint8_t synth_drums_default_velocity(synth_drum_voice_t voice, bool accented);

#ifdef __cplusplus
}
#endif

#endif
