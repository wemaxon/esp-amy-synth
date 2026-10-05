#ifndef SYNTH_CHORD_ENGINE_H
#define SYNTH_CHORD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "hmi/control_direction.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_CHORD_BUTTON_COUNT 7
#define SYNTH_MAX_CHORD_NOTES    8
#define SYNTH_DEFAULT_ROOT_MIDI  60
#define SYNTH_CHORD_INVERSION_COUNT 3

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

/* HiChord order (companion-app CC 103 values). Pentatonic and blues button
 * chords are not published; those scales are reserved and unavailable. */
typedef enum {
    SYNTH_SCALE_MAJOR = 0,
    SYNTH_SCALE_NATURAL_MINOR,
    SYNTH_SCALE_HARMONIC_MINOR,
    SYNTH_SCALE_MELODIC_MINOR,
    SYNTH_SCALE_MAJOR_PENTATONIC,
    SYNTH_SCALE_MINOR_PENTATONIC,
    SYNTH_SCALE_BLUES,
    SYNTH_SCALE_DORIAN,
    SYNTH_SCALE_MIXOLYDIAN,
    SYNTH_SCALE_LYDIAN,
    SYNTH_SCALE_COUNT,
} synth_scale_t;

typedef enum {
    SYNTH_BASS_MODE_OFF = 0,
    SYNTH_BASS_MODE_ROOT,
    SYNTH_BASS_MODE_SLASH,
    SYNTH_BASS_MODE_COUNT,
} synth_bass_mode_t;

typedef enum {
    SYNTH_JOYSTICK_MODE_DEFAULT = 0,
    SYNTH_JOYSTICK_MODE_EXTENDED,
    SYNTH_JOYSTICK_MODE_CHROMATIC,
    SYNTH_JOYSTICK_MODE_BORROW,
    SYNTH_JOYSTICK_MODE_OG,
    SYNTH_JOYSTICK_MODE_COUNT,
} synth_joystick_mode_t;

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
    SYNTH_CHORD_MOD_DOM7_SHARP9,
    SYNTH_CHORD_MOD_SUS4_FLAT7,
    SYNTH_CHORD_MOD_ADD11,
    SYNTH_CHORD_MOD_HALF_DIM7,
    SYNTH_CHORD_MOD_DOM9,
    SYNTH_CHORD_MOD_MIN11,
    SYNTH_CHORD_MOD_MIN_MAJ7,
    SYNTH_CHORD_MOD_MAJ13,
    SYNTH_CHORD_MOD_SIX_NINE,
    SYNTH_CHORD_MOD_MAJ7_SHARP11,
    SYNTH_CHORD_MOD_DOM13,
    SYNTH_CHORD_MOD_DOM7_FLAT9,
    SYNTH_CHORD_MOD_DOM7_ALT,
    SYNTH_CHORD_MOD_NINTH,
    SYNTH_CHORD_MOD_OG_NINTH,
    SYNTH_CHORD_MOD_FLAT_MAJOR,
    SYNTH_CHORD_MOD_PASSING_DIM7,
    SYNTH_CHORD_MOD_TRITONE_DOM7,
    SYNTH_CHORD_MOD_BACKDOOR_DOM7,
    SYNTH_CHORD_MOD_BORROWED_SEVENTH,
    SYNTH_CHORD_MOD_COUNT,
} synth_chord_modifier_t;

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    uint8_t degree;
    int8_t global_octave_shift;
    int8_t button_octave_shift;
    synth_chord_modifier_t modifier;
    synth_bass_mode_t bass_mode;
    int8_t inversion;
    uint8_t bass_degree;
    int8_t bass_octave_shift;
    bool voice_leading_enabled;
    uint8_t previous_notes[SYNTH_MAX_CHORD_NOTES];
    uint8_t previous_note_count;
} synth_chord_request_t;

typedef struct {
    uint8_t notes[SYNTH_MAX_CHORD_NOTES];
    uint8_t note_count;
    uint8_t degree;
    uint8_t root_midi;
    bool bass_enabled;
    uint8_t bass_note;
    synth_bass_mode_t bass_mode;
    synth_key_t key;
    synth_scale_t scale;
    synth_chord_modifier_t modifier;
    int8_t inversion;
    uint8_t bass_degree;
    bool voice_leading_enabled;
    char name[16];
} synth_chord_t;

const char *synth_key_name(synth_key_t key);
const char *synth_scale_name(synth_scale_t scale);
bool synth_scale_is_available(synth_scale_t scale);
/* Steps through available scales only, wrapping in HiChord menu order. */
synth_scale_t synth_scale_step(synth_scale_t scale, int8_t steps);
const char *synth_bass_mode_name(synth_bass_mode_t mode);
const char *synth_joystick_mode_name(synth_joystick_mode_t mode);
const char *synth_chord_modifier_name(synth_chord_modifier_t modifier);
synth_key_t synth_key_step(synth_key_t key, int8_t semitones);
synth_bass_mode_t synth_bass_mode_step(synth_bass_mode_t mode, int8_t steps);
synth_joystick_mode_t synth_joystick_mode_step(synth_joystick_mode_t mode, int8_t steps);
synth_chord_modifier_t synth_chord_modifier_from_joystick(synth_joystick_mode_t mode,
                                                          control_joystick_direction_t direction);
/* Chromatic horizontal gestures are key actions, not lockable chord modifiers. */
int8_t synth_chord_key_shift(synth_joystick_mode_t mode,
                            control_joystick_direction_t direction);
/* Pure, allocation-free operation. Invalid requests return -1 without changing output.
 * Note order retains root/third/fifth/extension identity after inversions.
 * An empty voice-leading history uses the requested manual inversion. */
int synth_build_chord(const synth_chord_request_t *request, synth_chord_t *chord);

#ifdef __cplusplus
}
#endif

#endif
