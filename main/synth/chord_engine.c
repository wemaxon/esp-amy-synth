#include "synth/chord_engine.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    BASE_MAJOR = 0,
    BASE_MINOR,
    BASE_DIMINISHED,
} base_chord_t;

static const uint8_t MAJOR_SCALE_INTERVALS[SYNTH_CHORD_BUTTON_COUNT] = {0, 2, 4, 5, 7, 9, 11};
static const base_chord_t MAJOR_SCALE_CHORDS[SYNTH_CHORD_BUTTON_COUNT] = {
    BASE_MAJOR,
    BASE_MINOR,
    BASE_MINOR,
    BASE_MAJOR,
    BASE_MAJOR,
    BASE_MINOR,
    BASE_DIMINISHED,
};

static const char *KEY_NAMES[SYNTH_KEY_COUNT] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

const char *synth_key_name(synth_key_t key)
{
    return key < SYNTH_KEY_COUNT ? KEY_NAMES[key] : "?";
}

const char *synth_scale_name(synth_scale_t scale)
{
    switch (scale) {
    case SYNTH_SCALE_MAJOR:
        return "Major";
    default:
        return "?";
    }
}

const char *synth_chord_modifier_name(synth_chord_modifier_t modifier)
{
    switch (modifier) {
    case SYNTH_CHORD_MOD_NONE:
        return "none";
    case SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR:
        return "maj/min";
    case SYNTH_CHORD_MOD_DOM7:
        return "dom7";
    case SYNTH_CHORD_MOD_MAJ7_MIN7:
        return "7";
    case SYNTH_CHORD_MOD_ADD9:
        return "add9";
    case SYNTH_CHORD_MOD_SUS4:
        return "sus4";
    case SYNTH_CHORD_MOD_SIXTH_OR_SUS2:
        return "6/sus2";
    case SYNTH_CHORD_MOD_DIM_OR_MINOR:
        return "dim/min";
    case SYNTH_CHORD_MOD_AUGMENTED:
        return "aug";
    default:
        return "?";
    }
}

synth_key_t synth_key_step(synth_key_t key, int8_t semitones)
{
    int value = (int)key + semitones;
    while (value < 0) {
        value += SYNTH_KEY_COUNT;
    }
    return (synth_key_t)(value % SYNTH_KEY_COUNT);
}

synth_chord_modifier_t synth_chord_modifier_from_joystick(control_joystick_direction_t direction)
{
    switch (direction) {
    case CONTROL_JOYSTICK_UP:
        return SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR;
    case CONTROL_JOYSTICK_UP_RIGHT:
        return SYNTH_CHORD_MOD_DOM7;
    case CONTROL_JOYSTICK_RIGHT:
        return SYNTH_CHORD_MOD_MAJ7_MIN7;
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        return SYNTH_CHORD_MOD_ADD9;
    case CONTROL_JOYSTICK_DOWN:
        return SYNTH_CHORD_MOD_SUS4;
    case CONTROL_JOYSTICK_DOWN_LEFT:
        return SYNTH_CHORD_MOD_SIXTH_OR_SUS2;
    case CONTROL_JOYSTICK_LEFT:
        return SYNTH_CHORD_MOD_DIM_OR_MINOR;
    case CONTROL_JOYSTICK_UP_LEFT:
        return SYNTH_CHORD_MOD_AUGMENTED;
    case CONTROL_JOYSTICK_CENTER:
    default:
        return SYNTH_CHORD_MOD_NONE;
    }
}

static void add_notes(synth_chord_t *chord, uint8_t root, const uint8_t *intervals, uint8_t count)
{
    chord->note_count = count > SYNTH_MAX_CHORD_NOTES ? SYNTH_MAX_CHORD_NOTES : count;
    for (uint8_t i = 0; i < chord->note_count; ++i) {
        chord->notes[i] = root + intervals[i];
    }
}

static const char *base_suffix(base_chord_t base)
{
    switch (base) {
    case BASE_MAJOR:
        return "";
    case BASE_MINOR:
        return "m";
    case BASE_DIMINISHED:
        return "dim";
    default:
        return "";
    }
}

static void set_chord_name(synth_chord_t *chord, const char *suffix)
{
    snprintf(chord->name, sizeof(chord->name), "%s%s", synth_key_name((synth_key_t)(chord->root_midi % 12)), suffix);
}

int synth_build_chord(const synth_chord_request_t *request, synth_chord_t *chord)
{
    if (request == NULL || chord == NULL ||
        request->key >= SYNTH_KEY_COUNT ||
        request->scale >= SYNTH_SCALE_COUNT ||
        request->degree == 0 ||
        request->degree > SYNTH_CHORD_BUTTON_COUNT) {
        return -1;
    }

    static const uint8_t MAJOR_TRIAD[] = {0, 4, 7};
    static const uint8_t MINOR_TRIAD[] = {0, 3, 7};
    static const uint8_t DIM_TRIAD[] = {0, 3, 6};
    static const uint8_t AUG_TRIAD[] = {0, 4, 8};
    static const uint8_t DOM7[] = {0, 4, 7, 10};
    static const uint8_t MAJ7[] = {0, 4, 7, 11};
    static const uint8_t MIN7[] = {0, 3, 7, 10};
    static const uint8_t ADD9_MAJOR[] = {0, 4, 7, 14};
    static const uint8_t ADD9_MINOR[] = {0, 3, 7, 14};
    static const uint8_t SUS4[] = {0, 5, 7};
    static const uint8_t MAJOR6[] = {0, 4, 7, 9};
    static const uint8_t SUS2[] = {0, 2, 7};

    memset(chord, 0, sizeof(*chord));
    chord->degree = request->degree;
    chord->key = request->key;
    chord->scale = request->scale;
    chord->modifier = request->modifier;

    int16_t root = SYNTH_DEFAULT_ROOT_MIDI +
                   (int16_t)request->key +
                   MAJOR_SCALE_INTERVALS[request->degree - 1] +
                   ((int16_t)request->global_octave_shift * 12) +
                   ((int16_t)request->button_octave_shift * 12);
    if (root < 0 || root > 119) {
        return -1;
    }

    chord->root_midi = (uint8_t)root;
    base_chord_t base = MAJOR_SCALE_CHORDS[request->degree - 1];
    const char *suffix = base_suffix(base);

    switch (request->modifier) {
    case SYNTH_CHORD_MOD_NONE:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, MAJOR_TRIAD, sizeof(MAJOR_TRIAD));
        } else if (base == BASE_MINOR) {
            add_notes(chord, chord->root_midi, MINOR_TRIAD, sizeof(MINOR_TRIAD));
        } else {
            add_notes(chord, chord->root_midi, DIM_TRIAD, sizeof(DIM_TRIAD));
        }
        break;
    case SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, MINOR_TRIAD, sizeof(MINOR_TRIAD));
            suffix = "m";
        } else {
            add_notes(chord, chord->root_midi, MAJOR_TRIAD, sizeof(MAJOR_TRIAD));
            suffix = "";
        }
        break;
    case SYNTH_CHORD_MOD_DOM7:
        add_notes(chord, chord->root_midi, DOM7, sizeof(DOM7));
        suffix = "7";
        break;
    case SYNTH_CHORD_MOD_MAJ7_MIN7:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, MAJ7, sizeof(MAJ7));
            suffix = "maj7";
        } else {
            add_notes(chord, chord->root_midi, MIN7, sizeof(MIN7));
            suffix = "m7";
        }
        break;
    case SYNTH_CHORD_MOD_ADD9:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, ADD9_MAJOR, sizeof(ADD9_MAJOR));
        } else {
            add_notes(chord, chord->root_midi, ADD9_MINOR, sizeof(ADD9_MINOR));
        }
        suffix = base == BASE_MAJOR ? "add9" : "madd9";
        break;
    case SYNTH_CHORD_MOD_SUS4:
        add_notes(chord, chord->root_midi, SUS4, sizeof(SUS4));
        suffix = "sus4";
        break;
    case SYNTH_CHORD_MOD_SIXTH_OR_SUS2:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, MAJOR6, sizeof(MAJOR6));
            suffix = "6";
        } else {
            add_notes(chord, chord->root_midi, SUS2, sizeof(SUS2));
            suffix = "sus2";
        }
        break;
    case SYNTH_CHORD_MOD_DIM_OR_MINOR:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, MINOR_TRIAD, sizeof(MINOR_TRIAD));
            suffix = "m";
        } else {
            add_notes(chord, chord->root_midi, DIM_TRIAD, sizeof(DIM_TRIAD));
            suffix = "dim";
        }
        break;
    case SYNTH_CHORD_MOD_AUGMENTED:
        add_notes(chord, chord->root_midi, AUG_TRIAD, sizeof(AUG_TRIAD));
        suffix = "aug";
        break;
    default:
        return -1;
    }

    set_chord_name(chord, suffix);
    return 0;
}
