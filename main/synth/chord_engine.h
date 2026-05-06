#ifndef SYNTH_CHORD_ENGINE_H
#define SYNTH_CHORD_ENGINE_H

#include <stdint.h>

#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_CHORD_BUTTON_COUNT 7
#define SYNTH_MAX_CHORD_NOTES    8
#define SYNTH_DEFAULT_ROOT_MIDI  60

typedef enum {
    SYNTH_KEY_C = 0,
    SYNTH_KEY_C_SHARP,
    SYNTH_KEY_D,
    SYNTH_KEY_D_SHARP,
    SYNTH_KEY_E,
    SYNTH_KEY_F,
    SYNTH_KEY_F_SHARP,
    SYNTH_KEY_G,
    SYNTH_KEY_G_SHARP,
    SYNTH_KEY_A,
    SYNTH_KEY_A_SHARP,
    SYNTH_KEY_B,
    SYNTH_KEY_COUNT,
} synth_key_t;

typedef enum {
    SYNTH_SCALE_MAJOR = 0,
    SYNTH_SCALE_COUNT,
} synth_scale_t;

typedef enum {
    SYNTH_CHORD_MOD_NONE = 0,
    SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR,
    SYNTH_CHORD_MOD_DOM7,
    SYNTH_CHORD_MOD_MAJ7_MIN7,
    SYNTH_CHORD_MOD_ADD9,
    SYNTH_CHORD_MOD_SUS4,
    SYNTH_CHORD_MOD_SIXTH_OR_SUS2,
    SYNTH_CHORD_MOD_DIM_OR_MINOR,
    SYNTH_CHORD_MOD_AUGMENTED,
} synth_chord_modifier_t;

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    uint8_t degree;
    int8_t global_octave_shift;
    int8_t button_octave_shift;
    synth_chord_modifier_t modifier;
} synth_chord_request_t;

typedef struct {
    uint8_t notes[SYNTH_MAX_CHORD_NOTES];
    uint8_t note_count;
    uint8_t degree;
    uint8_t root_midi;
    synth_key_t key;
    synth_scale_t scale;
    synth_chord_modifier_t modifier;
    char name[16];
} synth_chord_t;

const char *synth_key_name(synth_key_t key);
const char *synth_scale_name(synth_scale_t scale);
const char *synth_chord_modifier_name(synth_chord_modifier_t modifier);
synth_key_t synth_key_step(synth_key_t key, int8_t semitones);
synth_chord_modifier_t synth_chord_modifier_from_joystick(control_joystick_direction_t direction);
int synth_build_chord(const synth_chord_request_t *request, synth_chord_t *chord);

#ifdef __cplusplus
}
#endif

#endif
