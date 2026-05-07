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

static const char *JOYSTICK_MODE_NAMES[SYNTH_JOYSTICK_MODE_COUNT] = {
    [SYNTH_JOYSTICK_MODE_DEFAULT] = "DEFAULT",
    [SYNTH_JOYSTICK_MODE_EXTENDED] = "EXTENDED",
    [SYNTH_JOYSTICK_MODE_CHROMATIC] = "CHROMATIC",
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

const char *synth_joystick_mode_name(synth_joystick_mode_t mode)
{
    return mode < SYNTH_JOYSTICK_MODE_COUNT ? JOYSTICK_MODE_NAMES[mode] : "?";
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
        return "dim";
    case SYNTH_CHORD_MOD_AUGMENTED:
        return "aug";
    case SYNTH_CHORD_MOD_DOM7_SHARP9:
        return "7#9";
    case SYNTH_CHORD_MOD_SUS4_FLAT7:
        return "7sus4";
    case SYNTH_CHORD_MOD_ADD11:
        return "add11";
    case SYNTH_CHORD_MOD_HALF_DIM7:
        return "m7b5";
    case SYNTH_CHORD_MOD_DOM9:
        return "9";
    case SYNTH_CHORD_MOD_MIN11:
        return "m11";
    case SYNTH_CHORD_MOD_MIN_MAJ7:
        return "mMaj7";
    case SYNTH_CHORD_MOD_MAJ13:
        return "maj13";
    case SYNTH_CHORD_MOD_SIX_NINE:
        return "6/9";
    case SYNTH_CHORD_MOD_MAJ7_SHARP11:
        return "maj7#11";
    case SYNTH_CHORD_MOD_DOM13:
        return "13";
    case SYNTH_CHORD_MOD_DOM7_FLAT9:
        return "7b9";
    case SYNTH_CHORD_MOD_DOM7_ALT:
        return "7alt";
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

synth_joystick_mode_t synth_joystick_mode_step(synth_joystick_mode_t mode, int8_t steps)
{
    int value = (int)mode + steps;
    while (value < 0) {
        value += SYNTH_JOYSTICK_MODE_COUNT;
    }
    return (synth_joystick_mode_t)(value % SYNTH_JOYSTICK_MODE_COUNT);
}

static synth_chord_modifier_t default_modifier_from_joystick(control_joystick_direction_t direction)
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

static synth_chord_modifier_t extended_modifier_from_joystick(control_joystick_direction_t direction)
{
    switch (direction) {
    case CONTROL_JOYSTICK_UP:
        return SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR;
    case CONTROL_JOYSTICK_UP_RIGHT:
        return SYNTH_CHORD_MOD_DOM9;
    case CONTROL_JOYSTICK_RIGHT:
        return SYNTH_CHORD_MOD_ADD11;
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        return SYNTH_CHORD_MOD_MIN11;
    case CONTROL_JOYSTICK_DOWN:
        return SYNTH_CHORD_MOD_DOM7_SHARP9;
    case CONTROL_JOYSTICK_DOWN_LEFT:
        return SYNTH_CHORD_MOD_ADD9;
    case CONTROL_JOYSTICK_LEFT:
        return SYNTH_CHORD_MOD_SUS4_FLAT7;
    case CONTROL_JOYSTICK_UP_LEFT:
        return SYNTH_CHORD_MOD_HALF_DIM7;
    case CONTROL_JOYSTICK_CENTER:
    default:
        return SYNTH_CHORD_MOD_NONE;
    }
}

static synth_chord_modifier_t chromatic_modifier_from_joystick(control_joystick_direction_t direction)
{
    switch (direction) {
    case CONTROL_JOYSTICK_UP:
        return SYNTH_CHORD_MOD_MIN_MAJ7;
    case CONTROL_JOYSTICK_UP_RIGHT:
        return SYNTH_CHORD_MOD_DOM13;
    case CONTROL_JOYSTICK_RIGHT:
        return SYNTH_CHORD_MOD_SIX_NINE;
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        return SYNTH_CHORD_MOD_DOM7_ALT;
    case CONTROL_JOYSTICK_DOWN:
        return SYNTH_CHORD_MOD_MAJ13;
    case CONTROL_JOYSTICK_DOWN_LEFT:
        return SYNTH_CHORD_MOD_DOM7_FLAT9;
    case CONTROL_JOYSTICK_LEFT:
        return SYNTH_CHORD_MOD_HALF_DIM7;
    case CONTROL_JOYSTICK_UP_LEFT:
        return SYNTH_CHORD_MOD_MAJ7_SHARP11;
    case CONTROL_JOYSTICK_CENTER:
    default:
        return SYNTH_CHORD_MOD_NONE;
    }
}

synth_chord_modifier_t synth_chord_modifier_from_joystick(synth_joystick_mode_t mode,
                                                          control_joystick_direction_t direction)
{
    switch (mode) {
    case SYNTH_JOYSTICK_MODE_EXTENDED:
        return extended_modifier_from_joystick(direction);
    case SYNTH_JOYSTICK_MODE_CHROMATIC:
        return chromatic_modifier_from_joystick(direction);
    case SYNTH_JOYSTICK_MODE_DEFAULT:
    default:
        return default_modifier_from_joystick(direction);
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
    static const uint8_t HALF_DIM7[] = {0, 3, 6, 10};
    static const uint8_t ADD9_MAJOR[] = {0, 4, 7, 14};
    static const uint8_t ADD9_MINOR[] = {0, 3, 7, 14};
    static const uint8_t ADD9_DIM[] = {0, 3, 6, 14};
    static const uint8_t SUS4[] = {0, 5, 7};
    static const uint8_t MAJOR6[] = {0, 4, 7, 9};
    static const uint8_t SUS2[] = {0, 2, 7};
    static const uint8_t DOM7_SHARP9[] = {0, 4, 7, 10, 15};
    static const uint8_t SUS4_FLAT7[] = {0, 5, 7, 10};
    static const uint8_t ADD11_MAJOR[] = {0, 4, 7, 17};
    static const uint8_t ADD11_MINOR[] = {0, 3, 7, 17};
    static const uint8_t ADD11_DIM[] = {0, 3, 6, 17};
    static const uint8_t DOM9[] = {0, 4, 7, 10, 14};
    static const uint8_t MIN11[] = {0, 3, 7, 10, 17};
    static const uint8_t MIN_MAJ7[] = {0, 3, 7, 11};
    static const uint8_t MAJ13[] = {0, 4, 7, 11, 14, 21};
    static const uint8_t SIX_NINE[] = {0, 4, 7, 9, 14};
    static const uint8_t MAJ7_SHARP11[] = {0, 4, 7, 11, 18};
    static const uint8_t DOM13[] = {0, 4, 7, 10, 14, 21};
    static const uint8_t DOM7_FLAT9[] = {0, 4, 7, 10, 13};
    static const uint8_t DOM7_ALT[] = {0, 4, 6, 10, 13, 15};

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
        } else if (base == BASE_MINOR) {
            add_notes(chord, chord->root_midi, MAJOR_TRIAD, sizeof(MAJOR_TRIAD));
            suffix = "";
        } else {
            add_notes(chord, chord->root_midi, MINOR_TRIAD, sizeof(MINOR_TRIAD));
            suffix = "m";
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
        } else if (base == BASE_MINOR) {
            add_notes(chord, chord->root_midi, MIN7, sizeof(MIN7));
            suffix = "m7";
        } else {
            add_notes(chord, chord->root_midi, HALF_DIM7, sizeof(HALF_DIM7));
            suffix = "m7b5";
        }
        break;
    case SYNTH_CHORD_MOD_ADD9:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, ADD9_MAJOR, sizeof(ADD9_MAJOR));
            suffix = "add9";
        } else if (base == BASE_MINOR) {
            add_notes(chord, chord->root_midi, ADD9_MINOR, sizeof(ADD9_MINOR));
            suffix = "madd9";
        } else {
            add_notes(chord, chord->root_midi, ADD9_DIM, sizeof(ADD9_DIM));
            suffix = "dimadd9";
        }
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
        add_notes(chord, chord->root_midi, DIM_TRIAD, sizeof(DIM_TRIAD));
        suffix = "dim";
        break;
    case SYNTH_CHORD_MOD_AUGMENTED:
        add_notes(chord, chord->root_midi, AUG_TRIAD, sizeof(AUG_TRIAD));
        suffix = "aug";
        break;
    case SYNTH_CHORD_MOD_DOM7_SHARP9:
        add_notes(chord, chord->root_midi, DOM7_SHARP9, sizeof(DOM7_SHARP9));
        suffix = "7#9";
        break;
    case SYNTH_CHORD_MOD_SUS4_FLAT7:
        add_notes(chord, chord->root_midi, SUS4_FLAT7, sizeof(SUS4_FLAT7));
        suffix = "7sus4";
        break;
    case SYNTH_CHORD_MOD_ADD11:
        if (base == BASE_MAJOR) {
            add_notes(chord, chord->root_midi, ADD11_MAJOR, sizeof(ADD11_MAJOR));
        } else if (base == BASE_MINOR) {
            add_notes(chord, chord->root_midi, ADD11_MINOR, sizeof(ADD11_MINOR));
        } else {
            add_notes(chord, chord->root_midi, ADD11_DIM, sizeof(ADD11_DIM));
        }
        suffix = "add11";
        break;
    case SYNTH_CHORD_MOD_HALF_DIM7:
        add_notes(chord, chord->root_midi, HALF_DIM7, sizeof(HALF_DIM7));
        suffix = "m7b5";
        break;
    case SYNTH_CHORD_MOD_DOM9:
        add_notes(chord, chord->root_midi, DOM9, sizeof(DOM9));
        suffix = "9";
        break;
    case SYNTH_CHORD_MOD_MIN11:
        add_notes(chord, chord->root_midi, MIN11, sizeof(MIN11));
        suffix = "m11";
        break;
    case SYNTH_CHORD_MOD_MIN_MAJ7:
        add_notes(chord, chord->root_midi, MIN_MAJ7, sizeof(MIN_MAJ7));
        suffix = "mMaj7";
        break;
    case SYNTH_CHORD_MOD_MAJ13:
        add_notes(chord, chord->root_midi, MAJ13, sizeof(MAJ13));
        suffix = "maj13";
        break;
    case SYNTH_CHORD_MOD_SIX_NINE:
        add_notes(chord, chord->root_midi, SIX_NINE, sizeof(SIX_NINE));
        suffix = "6/9";
        break;
    case SYNTH_CHORD_MOD_MAJ7_SHARP11:
        add_notes(chord, chord->root_midi, MAJ7_SHARP11, sizeof(MAJ7_SHARP11));
        suffix = "maj7#11";
        break;
    case SYNTH_CHORD_MOD_DOM13:
        add_notes(chord, chord->root_midi, DOM13, sizeof(DOM13));
        suffix = "13";
        break;
    case SYNTH_CHORD_MOD_DOM7_FLAT9:
        add_notes(chord, chord->root_midi, DOM7_FLAT9, sizeof(DOM7_FLAT9));
        suffix = "7b9";
        break;
    case SYNTH_CHORD_MOD_DOM7_ALT:
        add_notes(chord, chord->root_midi, DOM7_ALT, sizeof(DOM7_ALT));
        suffix = "7alt";
        break;
    default:
        return -1;
    }

    set_chord_name(chord, suffix);
    return 0;
}
