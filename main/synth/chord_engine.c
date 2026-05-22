#include "synth/chord_engine.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    BASE_MAJOR = 0,
    BASE_MINOR,
    BASE_DIMINISHED,
    BASE_AUGMENTED,
    BASE_SUS2,
    BASE_SUS4,
    BASE_SCALE,
} base_chord_t;

typedef struct {
    const char *name;
    uint8_t note_count;
    uint8_t intervals[7];
} scale_definition_t;

typedef struct {
    int16_t intervals[SYNTH_MAX_CHORD_NOTES];
    uint8_t count;
    const char *suffix;
    base_chord_t base;
} chord_formula_t;

static const scale_definition_t SCALE_DEFS[SYNTH_SCALE_COUNT] = {
    [SYNTH_SCALE_MAJOR] = {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    [SYNTH_SCALE_MINOR] = {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    [SYNTH_SCALE_HARMONIC_MINOR] = {"HARM MIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    [SYNTH_SCALE_MELODIC_MINOR] = {"MELOD MIN", 7, {0, 2, 3, 5, 7, 9, 11}},
    [SYNTH_SCALE_MAJOR_PENTATONIC] = {"MAJ PEN", 5, {0, 2, 4, 7, 9}},
    [SYNTH_SCALE_MINOR_PENTATONIC] = {"MIN PEN", 5, {0, 3, 5, 7, 10}},
    [SYNTH_SCALE_BLUES] = {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
    [SYNTH_SCALE_DORIAN] = {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    [SYNTH_SCALE_MIXOLYDIAN] = {"MIXOLYD", 7, {0, 2, 4, 5, 7, 9, 10}},
    [SYNTH_SCALE_LYDIAN] = {"LYDIAN", 7, {0, 2, 4, 6, 7, 9, 11}},
};

static const char *KEY_NAMES[SYNTH_KEY_COUNT] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

static const char *BASS_MODE_NAMES[SYNTH_BASS_MODE_COUNT] = {
    [SYNTH_BASS_MODE_OFF] = "OFF",
    [SYNTH_BASS_MODE_ROOT] = "ROOT",
    [SYNTH_BASS_MODE_SLASH] = "SLASH",
};

static const char *JOYSTICK_MODE_NAMES[SYNTH_JOYSTICK_MODE_COUNT] = {
    [SYNTH_JOYSTICK_MODE_DEFAULT] = "DEFAULT",
    [SYNTH_JOYSTICK_MODE_EXTENDED] = "EXTENDED",
    [SYNTH_JOYSTICK_MODE_CHROMATIC] = "CHROMATIC",
};

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

const char *synth_key_name(synth_key_t key)
{
    return key < SYNTH_KEY_COUNT ? KEY_NAMES[key] : "?";
}

const char *synth_scale_name(synth_scale_t scale)
{
    return scale < SYNTH_SCALE_COUNT ? SCALE_DEFS[scale].name : "?";
}

const char *synth_bass_mode_name(synth_bass_mode_t mode)
{
    return mode < SYNTH_BASS_MODE_COUNT ? BASS_MODE_NAMES[mode] : "?";
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
    case SYNTH_CHORD_MOD_MAJ9_MIN9:
        return "9";
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

synth_scale_t synth_scale_step(synth_scale_t scale, int8_t steps)
{
    int value = (int)scale + steps;
    while (value < 0) {
        value += SYNTH_SCALE_COUNT;
    }
    return (synth_scale_t)(value % SYNTH_SCALE_COUNT);
}

synth_bass_mode_t synth_bass_mode_step(synth_bass_mode_t mode, int8_t steps)
{
    int value = (int)mode + steps;
    while (value < 0) {
        value += SYNTH_BASS_MODE_COUNT;
    }
    return (synth_bass_mode_t)(value % SYNTH_BASS_MODE_COUNT);
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

static int16_t scale_degree_offset(const scale_definition_t *scale, int degree_index)
{
    int octave = 0;
    while (degree_index < 0) {
        degree_index += scale->note_count;
        --octave;
    }
    octave += degree_index / scale->note_count;
    int index = degree_index % scale->note_count;
    return (int16_t)scale->intervals[index] + (int16_t)(octave * 12);
}

static base_chord_t classify_triad(int16_t third, int16_t fifth)
{
    third %= 12;
    fifth %= 12;
    if (third < 0) {
        third += 12;
    }
    if (fifth < 0) {
        fifth += 12;
    }

    if (third == 4 && fifth == 7) {
        return BASE_MAJOR;
    }
    if (third == 3 && fifth == 7) {
        return BASE_MINOR;
    }
    if (third == 3 && fifth == 6) {
        return BASE_DIMINISHED;
    }
    if (third == 4 && fifth == 8) {
        return BASE_AUGMENTED;
    }
    if (third == 2 && fifth == 7) {
        return BASE_SUS2;
    }
    if (third == 5 && fifth == 7) {
        return BASE_SUS4;
    }
    return BASE_SCALE;
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
    case BASE_AUGMENTED:
        return "aug";
    case BASE_SUS2:
        return "sus2";
    case BASE_SUS4:
        return "sus4";
    case BASE_SCALE:
    default:
        return "sc";
    }
}

static void formula_set(chord_formula_t *formula,
                        const int16_t *intervals,
                        uint8_t count,
                        const char *suffix,
                        base_chord_t base)
{
    memset(formula, 0, sizeof(*formula));
    formula->count = count > SYNTH_MAX_CHORD_NOTES ? SYNTH_MAX_CHORD_NOTES : count;
    for (uint8_t i = 0; i < formula->count; ++i) {
        formula->intervals[i] = intervals[i];
    }
    formula->suffix = suffix;
    formula->base = base;
}

static void formula_append(chord_formula_t *formula, int16_t interval)
{
    if (formula->count >= SYNTH_MAX_CHORD_NOTES) {
        return;
    }
    formula->intervals[formula->count++] = interval;
}

static chord_formula_t scale_formula_for_degree(const scale_definition_t *scale, uint8_t degree)
{
    chord_formula_t formula = {0};
    int root_degree = (int)degree - 1;
    int16_t root = scale_degree_offset(scale, root_degree);
    int16_t intervals[3] = {
        0,
        (int16_t)(scale_degree_offset(scale, root_degree + 2) - root),
        (int16_t)(scale_degree_offset(scale, root_degree + 4) - root),
    };
    base_chord_t base = classify_triad(intervals[1], intervals[2]);
    formula_set(&formula, intervals, 3, base_suffix(base), base);
    return formula;
}

static bool base_is_majorish(base_chord_t base)
{
    return base == BASE_MAJOR || base == BASE_AUGMENTED || base == BASE_SCALE;
}

static bool base_is_minorish(base_chord_t base)
{
    return base == BASE_MINOR || base == BASE_DIMINISHED;
}

static chord_formula_t formula_for_modifier(synth_chord_modifier_t modifier, const chord_formula_t *base_formula)
{
    static const int16_t MAJOR_TRIAD[] = {0, 4, 7};
    static const int16_t MINOR_TRIAD[] = {0, 3, 7};
    static const int16_t DIM_TRIAD[] = {0, 3, 6};
    static const int16_t AUG_TRIAD[] = {0, 4, 8};
    static const int16_t DOM7[] = {0, 4, 7, 10};
    static const int16_t MAJ7[] = {0, 4, 7, 11};
    static const int16_t MIN7[] = {0, 3, 7, 10};
    static const int16_t HALF_DIM7[] = {0, 3, 6, 10};
    static const int16_t MAJ9[] = {0, 4, 7, 11, 14};
    static const int16_t MIN9[] = {0, 3, 7, 10, 14};
    static const int16_t HALF_DIM9[] = {0, 3, 6, 10, 14};
    static const int16_t ADD9_MAJOR[] = {0, 4, 7, 14};
    static const int16_t ADD9_MINOR[] = {0, 3, 7, 14};
    static const int16_t ADD9_DIM[] = {0, 3, 6, 14};
    static const int16_t SUS4[] = {0, 5, 7};
    static const int16_t MAJOR6[] = {0, 4, 7, 9};
    static const int16_t SUS2[] = {0, 2, 7};
    static const int16_t DOM7_SHARP9[] = {0, 4, 7, 10, 15};
    static const int16_t SUS4_FLAT7[] = {0, 5, 7, 10};
    static const int16_t ADD11_MAJOR[] = {0, 4, 7, 17};
    static const int16_t ADD11_MINOR[] = {0, 3, 7, 17};
    static const int16_t ADD11_DIM[] = {0, 3, 6, 17};
    static const int16_t DOM9[] = {0, 4, 7, 10, 14};
    static const int16_t MIN11[] = {0, 3, 7, 10, 17};
    static const int16_t MIN_MAJ7[] = {0, 3, 7, 11};
    static const int16_t MAJ13[] = {0, 4, 7, 11, 14, 21};
    static const int16_t SIX_NINE[] = {0, 4, 7, 9, 14};
    static const int16_t MAJ7_SHARP11[] = {0, 4, 7, 11, 18};
    static const int16_t DOM13[] = {0, 4, 7, 10, 14, 21};
    static const int16_t DOM7_FLAT9[] = {0, 4, 7, 10, 13};
    static const int16_t DOM7_ALT[] = {0, 4, 6, 10, 13, 15};

    chord_formula_t formula = *base_formula;
    switch (modifier) {
    case SYNTH_CHORD_MOD_NONE:
        return formula;
    case SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR:
        if (base_is_majorish(base_formula->base)) {
            formula_set(&formula, MINOR_TRIAD, 3, "m", BASE_MINOR);
        } else if (base_formula->base == BASE_DIMINISHED) {
            formula_set(&formula, MINOR_TRIAD, 3, "m", BASE_MINOR);
        } else {
            formula_set(&formula, MAJOR_TRIAD, 3, "", BASE_MAJOR);
        }
        return formula;
    case SYNTH_CHORD_MOD_DOM7:
        formula_set(&formula, DOM7, 4, "7", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_MAJ7_MIN7:
        if (base_formula->base == BASE_DIMINISHED) {
            formula_set(&formula, HALF_DIM7, 4, "m7b5", BASE_DIMINISHED);
        } else if (base_is_minorish(base_formula->base)) {
            formula_set(&formula, MIN7, 4, "m7", BASE_MINOR);
        } else {
            formula_set(&formula, MAJ7, 4, "maj7", BASE_MAJOR);
        }
        return formula;
    case SYNTH_CHORD_MOD_MAJ9_MIN9:
        if (base_formula->base == BASE_DIMINISHED) {
            formula_set(&formula, HALF_DIM9, 5, "m9b5", BASE_DIMINISHED);
        } else if (base_is_minorish(base_formula->base)) {
            formula_set(&formula, MIN9, 5, "m9", BASE_MINOR);
        } else {
            formula_set(&formula, MAJ9, 5, "maj9", BASE_MAJOR);
        }
        return formula;
    case SYNTH_CHORD_MOD_ADD9:
        if (base_formula->base == BASE_DIMINISHED) {
            formula_set(&formula, ADD9_DIM, 4, "dimadd9", BASE_DIMINISHED);
        } else if (base_is_minorish(base_formula->base)) {
            formula_set(&formula, ADD9_MINOR, 4, "madd9", BASE_MINOR);
        } else if (base_formula->base == BASE_MAJOR) {
            formula_set(&formula, ADD9_MAJOR, 4, "add9", BASE_MAJOR);
        } else {
            formula = *base_formula;
            formula_append(&formula, 14);
            formula.suffix = "add9";
        }
        return formula;
    case SYNTH_CHORD_MOD_SUS4:
        formula_set(&formula, SUS4, 3, "sus4", BASE_SUS4);
        return formula;
    case SYNTH_CHORD_MOD_SIXTH_OR_SUS2:
        if (base_is_majorish(base_formula->base)) {
            formula_set(&formula, MAJOR6, 4, "6", BASE_MAJOR);
        } else {
            formula_set(&formula, SUS2, 3, "sus2", BASE_SUS2);
        }
        return formula;
    case SYNTH_CHORD_MOD_DIM_OR_MINOR:
        formula_set(&formula, DIM_TRIAD, 3, "dim", BASE_DIMINISHED);
        return formula;
    case SYNTH_CHORD_MOD_AUGMENTED:
        formula_set(&formula, AUG_TRIAD, 3, "aug", BASE_AUGMENTED);
        return formula;
    case SYNTH_CHORD_MOD_DOM7_SHARP9:
        formula_set(&formula, DOM7_SHARP9, 5, "7#9", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_SUS4_FLAT7:
        formula_set(&formula, SUS4_FLAT7, 4, "7sus4", BASE_SUS4);
        return formula;
    case SYNTH_CHORD_MOD_ADD11:
        if (base_formula->base == BASE_DIMINISHED) {
            formula_set(&formula, ADD11_DIM, 4, "add11", BASE_DIMINISHED);
        } else if (base_is_minorish(base_formula->base)) {
            formula_set(&formula, ADD11_MINOR, 4, "add11", BASE_MINOR);
        } else if (base_formula->base == BASE_MAJOR) {
            formula_set(&formula, ADD11_MAJOR, 4, "add11", BASE_MAJOR);
        } else {
            formula = *base_formula;
            formula_append(&formula, 17);
            formula.suffix = "add11";
        }
        return formula;
    case SYNTH_CHORD_MOD_HALF_DIM7:
        formula_set(&formula, HALF_DIM7, 4, "m7b5", BASE_DIMINISHED);
        return formula;
    case SYNTH_CHORD_MOD_DOM9:
        formula_set(&formula, DOM9, 5, "9", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_MIN11:
        formula_set(&formula, MIN11, 5, "m11", BASE_MINOR);
        return formula;
    case SYNTH_CHORD_MOD_MIN_MAJ7:
        formula_set(&formula, MIN_MAJ7, 4, "mMaj7", BASE_MINOR);
        return formula;
    case SYNTH_CHORD_MOD_MAJ13:
        formula_set(&formula, MAJ13, 6, "maj13", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_SIX_NINE:
        formula_set(&formula, SIX_NINE, 5, "6/9", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_MAJ7_SHARP11:
        formula_set(&formula, MAJ7_SHARP11, 5, "maj7#11", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_DOM13:
        formula_set(&formula, DOM13, 6, "13", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_DOM7_FLAT9:
        formula_set(&formula, DOM7_FLAT9, 5, "7b9", BASE_MAJOR);
        return formula;
    case SYNTH_CHORD_MOD_DOM7_ALT:
        formula_set(&formula, DOM7_ALT, 6, "7alt", BASE_MAJOR);
        return formula;
    default:
        return formula;
    }
}

static uint8_t midi_in_range(int16_t note)
{
    while (note < 0) {
        note += 12;
    }
    while (note > 127) {
        note -= 12;
    }
    return (uint8_t)note;
}

static void sort_int16(int16_t *notes, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i) {
        int16_t note = notes[i];
        uint8_t j = i;
        while (j > 0 && notes[j - 1] > note) {
            notes[j] = notes[j - 1];
            --j;
        }
        notes[j] = note;
    }
}

static uint8_t min_note_index(const int16_t *notes, uint8_t count)
{
    uint8_t index = 0;
    for (uint8_t i = 1; i < count; ++i) {
        if (notes[i] < notes[index]) {
            index = i;
        }
    }
    return index;
}

static uint8_t max_note_index(const int16_t *notes, uint8_t count)
{
    uint8_t index = 0;
    for (uint8_t i = 1; i < count; ++i) {
        if (notes[i] > notes[index]) {
            index = i;
        }
    }
    return index;
}

static void apply_inversion(int16_t *notes, uint8_t count, int8_t inversion)
{
    if (notes == NULL || count == 0) {
        return;
    }

    while (inversion > 0) {
        notes[min_note_index(notes, count)] += 12;
        --inversion;
    }
    while (inversion < 0) {
        notes[max_note_index(notes, count)] -= 12;
        ++inversion;
    }
}

static int16_t average_note(const int16_t *notes, uint8_t count)
{
    if (notes == NULL || count == 0) {
        return 0;
    }

    int32_t total = 0;
    for (uint8_t i = 0; i < count; ++i) {
        total += notes[i];
    }
    return (int16_t)(total / count);
}

static int32_t voicing_score(const int16_t *candidate,
                             uint8_t candidate_count,
                             const uint8_t *previous,
                             uint8_t previous_count)
{
    int16_t cand_sorted[SYNTH_MAX_CHORD_NOTES] = {0};
    int16_t prev_sorted[SYNTH_MAX_CHORD_NOTES] = {0};
    uint8_t cand_count = candidate_count > SYNTH_MAX_CHORD_NOTES ? SYNTH_MAX_CHORD_NOTES : candidate_count;
    uint8_t prev_count = previous_count > SYNTH_MAX_CHORD_NOTES ? SYNTH_MAX_CHORD_NOTES : previous_count;

    for (uint8_t i = 0; i < cand_count; ++i) {
        cand_sorted[i] = candidate[i];
    }
    for (uint8_t i = 0; i < prev_count; ++i) {
        prev_sorted[i] = previous[i];
    }
    sort_int16(cand_sorted, cand_count);
    sort_int16(prev_sorted, prev_count);

    uint8_t pairs = cand_count < prev_count ? cand_count : prev_count;
    int32_t score = (int32_t)abs_int((int)average_note(cand_sorted, cand_count) -
                                     (int)average_note(prev_sorted, prev_count)) * 2;
    score += (int32_t)abs_int((int)cand_count - (int)prev_count) * 24;
    for (uint8_t i = 0; i < pairs; ++i) {
        score += (int32_t)abs_int((int)cand_sorted[i] - (int)prev_sorted[i]) * 5;
    }
    return score;
}

static void apply_voice_leading(int16_t *notes,
                                uint8_t count,
                                const uint8_t *previous,
                                uint8_t previous_count)
{
    if (notes == NULL || count == 0 || previous == NULL || previous_count == 0) {
        return;
    }

    int32_t best_score = INT_MAX;
    int16_t best_notes[SYNTH_MAX_CHORD_NOTES] = {0};
    for (int8_t inversion = 0; inversion < SYNTH_CHORD_INVERSION_COUNT; ++inversion) {
        int16_t candidate[SYNTH_MAX_CHORD_NOTES] = {0};
        for (uint8_t i = 0; i < count; ++i) {
            candidate[i] = notes[i];
        }
        apply_inversion(candidate, count, inversion);

        int32_t score = voicing_score(candidate, count, previous, previous_count);
        if (score < best_score) {
            best_score = score;
            for (uint8_t i = 0; i < count; ++i) {
                best_notes[i] = candidate[i];
            }
        }
    }

    for (uint8_t i = 0; i < count; ++i) {
        notes[i] = best_notes[i];
    }
}

static uint8_t lowest_chord_note(const synth_chord_t *chord)
{
    uint8_t lowest = chord->notes[0];
    for (uint8_t i = 1; i < chord->note_count; ++i) {
        if (chord->notes[i] < lowest) {
            lowest = chord->notes[i];
        }
    }
    return lowest;
}

static uint8_t bass_below_chord(int16_t bass, const synth_chord_t *chord)
{
    uint8_t lowest = chord->note_count > 0 ? lowest_chord_note(chord) : SYNTH_DEFAULT_ROOT_MIDI;
    while (bass >= (int16_t)lowest) {
        bass -= 12;
    }
    while (bass < 0) {
        bass += 12;
    }
    return midi_in_range(bass);
}

static void set_chord_name(synth_chord_t *chord, const char *suffix)
{
    const char *root = synth_key_name((synth_key_t)(chord->root_midi % 12));
    if (chord->bass_enabled &&
        chord->bass_mode == SYNTH_BASS_MODE_SLASH &&
        chord->bass_note % 12 != chord->root_midi % 12) {
        const char *slash = synth_key_name((synth_key_t)(chord->bass_note % 12));
        snprintf(chord->name, sizeof(chord->name), "%s%s/%s", root, suffix, slash);
        return;
    }
    snprintf(chord->name, sizeof(chord->name), "%s%s", root, suffix);
}

int synth_build_chord(const synth_chord_request_t *request, synth_chord_t *chord)
{
    if (request == NULL || chord == NULL ||
        request->key >= SYNTH_KEY_COUNT ||
        request->scale >= SYNTH_SCALE_COUNT ||
        request->degree == 0 ||
        request->degree > SYNTH_CHORD_BUTTON_COUNT ||
        request->modifier >= SYNTH_CHORD_MOD_COUNT ||
        request->bass_mode >= SYNTH_BASS_MODE_COUNT ||
        request->inversion < 0 ||
        request->inversion >= SYNTH_CHORD_INVERSION_COUNT ||
        (request->bass_mode == SYNTH_BASS_MODE_SLASH &&
         (request->bass_degree == 0 ||
          request->bass_degree > SYNTH_CHORD_BUTTON_COUNT))) {
        return -1;
    }

    memset(chord, 0, sizeof(*chord));
    chord->degree = request->degree;
    chord->key = request->key;
    chord->scale = request->scale;
    chord->modifier = request->modifier;
    chord->bass_mode = request->bass_mode;
    chord->inversion = request->inversion;
    chord->bass_degree = request->bass_degree;
    chord->voice_leading_enabled = request->voice_leading_enabled;

    const scale_definition_t *scale = &SCALE_DEFS[request->scale];
    int root_degree = (int)request->degree - 1;
    int16_t root = SYNTH_DEFAULT_ROOT_MIDI +
                   (int16_t)request->key +
                   scale_degree_offset(scale, root_degree) +
                   ((int16_t)request->global_octave_shift * 12) +
                   ((int16_t)request->button_octave_shift * 12);

    if (root < 0 || root > 127) {
        return -1;
    }

    chord->root_midi = (uint8_t)root;

    chord_formula_t scale_formula = scale_formula_for_degree(scale, request->degree);
    chord_formula_t formula = formula_for_modifier(request->modifier, &scale_formula);
    int16_t voiced[SYNTH_MAX_CHORD_NOTES] = {0};
    chord->note_count = formula.count;
    for (uint8_t i = 0; i < chord->note_count; ++i) {
        voiced[i] = root + formula.intervals[i];
    }

    if (request->voice_leading_enabled) {
        apply_voice_leading(voiced,
                            chord->note_count,
                            request->previous_notes,
                            request->previous_note_count);
    } else {
        apply_inversion(voiced, chord->note_count, request->inversion);
    }

    for (uint8_t i = 0; i < chord->note_count; ++i) {
        chord->notes[i] = midi_in_range(voiced[i]);
    }

    if (request->bass_mode == SYNTH_BASS_MODE_ROOT) {
        chord->bass_enabled = true;
        chord->bass_note = bass_below_chord(root - 24, chord);
    } else if (request->bass_mode == SYNTH_BASS_MODE_SLASH) {
        int16_t bass_root = SYNTH_DEFAULT_ROOT_MIDI +
                            (int16_t)request->key +
                            scale_degree_offset(scale, (int)request->bass_degree - 1) +
                            ((int16_t)request->global_octave_shift * 12) +
                            ((int16_t)request->bass_octave_shift * 12);
        if (bass_root < 0 || bass_root > 127) {
            return -1;
        }
        chord->bass_enabled = true;
        chord->bass_note = bass_below_chord(bass_root, chord);
    }

    set_chord_name(chord, formula.suffix);
    return 0;
}
