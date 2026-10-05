#ifndef SYNTH_APP_KEY_H
#define SYNTH_APP_KEY_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/chord_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_GLOBAL_OCTAVE_DEFAULT 0
#define SYNTH_APP_GLOBAL_OCTAVE_MIN -2
#define SYNTH_APP_GLOBAL_OCTAVE_MAX 2
#define SYNTH_APP_BUTTON_OCTAVE_MIN -2
#define SYNTH_APP_BUTTON_OCTAVE_MAX 2

bool synth_app_key_step(synth_key_t current_key,
                        int8_t current_global_octave,
                        int8_t semitones,
                        synth_key_t *next_key,
                        int8_t *next_global_octave);
bool synth_app_global_octave_step(int8_t current, int8_t octaves, int8_t *next);
bool synth_app_button_octave_step(int8_t current, int8_t octaves, int8_t *next);

#ifdef __cplusplus
}
#endif

#endif
