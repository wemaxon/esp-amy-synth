#include "synth/chord_engine.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* HiChord 2.9 public manual/app, checked 2026-09-21. See maintenance.md for
 * source conflicts and the explicitly chosen local voicing policies. */
typedef struct {
    const char *suffix;
    uint8_t count;
    uint8_t intervals[6];
} chord_formula_t;

typedef enum {
    MAJOR, MINOR, DIM, AUG, SUS2, SUS4, MAJOR6,
    DOM7, MAJ7, MIN7, HALF_DIM7, ADD9, MIN_ADD9, DIM_ADD9,
    DOM7_SHARP9, SUS4_FLAT7, ADD11, MIN_ADD11, DIM_ADD11, DOM9,
    MIN11, MIN_MAJ7, MAJ13, SIX_NINE, MAJ7_SHARP11, DOM13,
    DOM7_FLAT9, DOM7_ALT, MAJ9, MIN9, DIM9, OG_MAJ9, OG_MIN9,
    OG_DIM9, DIM7,
} formula_id_t;

/* Names and pitches have one owner. No generic scale/fallback chord builder. */
static const chord_formula_t FORMULAS[] = {
    [MAJOR] = {"", 3, {0, 4, 7}},
    [MINOR] = {"m", 3, {0, 3, 7}},
    [DIM] = {"dim", 3, {0, 3, 6}},
    [AUG] = {"aug", 3, {0, 4, 8}},
    [SUS2] = {"sus2", 3, {0, 2, 7}},
    [SUS4] = {"sus4", 3, {0, 5, 7}},
    [MAJOR6] = {"6", 4, {0, 4, 7, 9}},
    [DOM7] = {"7", 4, {0, 4, 7, 10}},
    [MAJ7] = {"maj7", 4, {0, 4, 7, 11}},
    [MIN7] = {"m7", 4, {0, 3, 7, 10}},
    [HALF_DIM7] = {"m7b5", 4, {0, 3, 6, 10}},
    [ADD9] = {"add9", 4, {0, 4, 7, 14}},
    [MIN_ADD9] = {"madd9", 4, {0, 3, 7, 14}},
    [DIM_ADD9] = {"dimadd9", 4, {0, 3, 6, 14}},
    [DOM7_SHARP9] = {"7#9", 5, {0, 4, 7, 10, 15}},
    [SUS4_FLAT7] = {"7sus4", 4, {0, 5, 7, 10}},
    [ADD11] = {"add11", 4, {0, 4, 7, 17}},
    [MIN_ADD11] = {"madd11", 4, {0, 3, 7, 17}},
    [DIM_ADD11] = {"dimadd11", 4, {0, 3, 6, 17}},
    [DOM9] = {"9", 5, {0, 4, 7, 10, 14}},
    [MIN11] = {"m11", 5, {0, 3, 7, 10, 17}},
    [MIN_MAJ7] = {"mMaj7", 4, {0, 3, 7, 11}},
    [MAJ13] = {"maj13", 6, {0, 4, 7, 11, 14, 21}},
    [SIX_NINE] = {"6/9", 5, {0, 4, 7, 9, 14}},
    [MAJ7_SHARP11] = {"maj7#11", 5, {0, 4, 7, 11, 18}},
    [DOM13] = {"13", 6, {0, 4, 7, 10, 14, 21}},
    [DOM7_FLAT9] = {"7b9", 5, {0, 4, 7, 10, 13}},
    [DOM7_ALT] = {"7alt", 6, {0, 4, 6, 10, 13, 15}},
    [MAJ9] = {"maj9", 5, {0, 4, 7, 11, 14}},
    [MIN9] = {"m9", 5, {0, 3, 7, 10, 14}},
    [DIM9] = {"m9b5", 5, {0, 3, 6, 10, 14}},
    /* OG deliberately replaces the ninth with an octave-doubled third. */
    [OG_MAJ9] = {"maj9", 5, {0, 4, 7, 11, 16}},
    [OG_MIN9] = {"m9", 5, {0, 3, 7, 10, 15}},
    [OG_DIM9] = {"m9b5", 5, {0, 3, 6, 10, 15}},
    [DIM7] = {"dim7", 4, {0, 3, 6, 9}},
};

typedef struct {
    const char *name;
    formula_id_t family[3]; /* major, minor, diminished */
    int8_t root_shift;
} modifier_definition_t;

static const modifier_definition_t MODIFIERS[SYNTH_CHORD_MOD_COUNT] = {
    [SYNTH_CHORD_MOD_NONE] = {"none", {MAJOR, MINOR, DIM}, 0},
    [SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR] = {"maj/min", {MINOR, MAJOR, MINOR}, 0},
    [SYNTH_CHORD_MOD_DOM7] = {"dom7", {DOM7, DOM7, DOM7}, 0},
    [SYNTH_CHORD_MOD_MAJ7_MIN7] = {"7", {MAJ7, MIN7, HALF_DIM7}, 0},
    [SYNTH_CHORD_MOD_ADD9] = {"add9", {ADD9, MIN_ADD9, DIM_ADD9}, 0},
    [SYNTH_CHORD_MOD_SUS4] = {"sus4", {SUS4, SUS4, SUS4}, 0},
    [SYNTH_CHORD_MOD_SIXTH_OR_SUS2] = {"6/sus2", {MAJOR6, SUS2, SUS2}, 0},
    [SYNTH_CHORD_MOD_DIM_OR_MINOR] = {"dim/min", {MINOR, DIM, DIM}, 0},
    [SYNTH_CHORD_MOD_AUGMENTED] = {"aug", {AUG, AUG, AUG}, 0},
    [SYNTH_CHORD_MOD_DOM7_SHARP9] = {"7#9", {DOM7_SHARP9, DOM7_SHARP9, DOM7_SHARP9}, 0},
    [SYNTH_CHORD_MOD_SUS4_FLAT7] = {"7sus4", {SUS4_FLAT7, SUS4_FLAT7, SUS4_FLAT7}, 0},
    [SYNTH_CHORD_MOD_ADD11] = {"add11", {ADD11, MIN_ADD11, DIM_ADD11}, 0},
    [SYNTH_CHORD_MOD_HALF_DIM7] = {"m7b5", {HALF_DIM7, HALF_DIM7, HALF_DIM7}, 0},
    [SYNTH_CHORD_MOD_DOM9] = {"9", {DOM9, DOM9, DOM9}, 0},
    [SYNTH_CHORD_MOD_MIN11] = {"m11", {MIN11, MIN11, MIN11}, 0},
    [SYNTH_CHORD_MOD_MIN_MAJ7] = {"mMaj7", {MIN_MAJ7, MIN_MAJ7, MIN_MAJ7}, 0},
    [SYNTH_CHORD_MOD_MAJ13] = {"maj13", {MAJ13, MAJ13, MAJ13}, 0},
    [SYNTH_CHORD_MOD_SIX_NINE] = {"6/9", {SIX_NINE, SIX_NINE, SIX_NINE}, 0},
    [SYNTH_CHORD_MOD_MAJ7_SHARP11] = {"maj7#11", {MAJ7_SHARP11, MAJ7_SHARP11, MAJ7_SHARP11}, 0},
    [SYNTH_CHORD_MOD_DOM13] = {"13", {DOM13, DOM13, DOM13}, 0},
    [SYNTH_CHORD_MOD_DOM7_FLAT9] = {"7b9", {DOM7_FLAT9, DOM7_FLAT9, DOM7_FLAT9}, 0},
    [SYNTH_CHORD_MOD_DOM7_ALT] = {"7alt", {DOM7_ALT, DOM7_ALT, DOM7_ALT}, 0},
    [SYNTH_CHORD_MOD_NINTH] = {"9th", {MAJ9, MIN9, DIM9}, 0},
    [SYNTH_CHORD_MOD_OG_NINTH] = {"OG 9th", {OG_MAJ9, OG_MIN9, OG_DIM9}, 0},
    [SYNTH_CHORD_MOD_FLAT_MAJOR] = {"bMajor", {MAJOR, MAJOR, MAJOR}, -1},
    [SYNTH_CHORD_MOD_PASSING_DIM7] = {"pass dim7", {DIM7, DIM7, DIM7}, 1},
    [SYNTH_CHORD_MOD_TRITONE_DOM7] = {"tritone7", {DOM7, DOM7, DOM7}, -6},
    [SYNTH_CHORD_MOD_BACKDOOR_DOM7] = {"backdoor7", {DOM7, DOM7, DOM7}, -2},
    [SYNTH_CHORD_MOD_BORROWED_SEVENTH] = {"borrow7", {MIN7, MAJ7, MAJ7}, 0},
};

static const uint8_t DEGREE_ROOTS[7] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t DEGREE_FAMILIES[7] = {0, 1, 1, 0, 0, 1, 2};
static const char *const KEY_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const BASS_NAMES[] = {"OFF", "ROOT", "SLASH"};
static const char *const MODE_NAMES[] = {"DEFAULT", "EXTENDED", "CHROMATIC", "BORROW", "OG"};

/* Center, N, NE, E, SE, S, SW, W, NW: same order as control_direction.h. */
static const synth_chord_modifier_t JOYSTICK_MAP[SYNTH_JOYSTICK_MODE_COUNT][9] = {
    {SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR, SYNTH_CHORD_MOD_DOM7,
     SYNTH_CHORD_MOD_MAJ7_MIN7, SYNTH_CHORD_MOD_NINTH, SYNTH_CHORD_MOD_SUS4,
     SYNTH_CHORD_MOD_SIXTH_OR_SUS2, SYNTH_CHORD_MOD_DIM_OR_MINOR, SYNTH_CHORD_MOD_AUGMENTED},
    {SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR, SYNTH_CHORD_MOD_DOM9,
     SYNTH_CHORD_MOD_ADD11, SYNTH_CHORD_MOD_MIN11, SYNTH_CHORD_MOD_DOM7_SHARP9,
     SYNTH_CHORD_MOD_ADD9, SYNTH_CHORD_MOD_SUS4_FLAT7, SYNTH_CHORD_MOD_HALF_DIM7},
    {SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_MIN_MAJ7, SYNTH_CHORD_MOD_DOM7_ALT,
     SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_HALF_DIM7, SYNTH_CHORD_MOD_MAJ13,
     SYNTH_CHORD_MOD_SIX_NINE, SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_DOM7_FLAT9},
    {SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_DOM7, SYNTH_CHORD_MOD_BACKDOOR_DOM7,
     SYNTH_CHORD_MOD_PASSING_DIM7, SYNTH_CHORD_MOD_HALF_DIM7, SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR,
     SYNTH_CHORD_MOD_BORROWED_SEVENTH, SYNTH_CHORD_MOD_FLAT_MAJOR, SYNTH_CHORD_MOD_TRITONE_DOM7},
    {SYNTH_CHORD_MOD_NONE, SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR, SYNTH_CHORD_MOD_DOM7,
     SYNTH_CHORD_MOD_MAJ7_MIN7, SYNTH_CHORD_MOD_OG_NINTH, SYNTH_CHORD_MOD_SUS4,
     SYNTH_CHORD_MOD_SIXTH_OR_SUS2, SYNTH_CHORD_MOD_DIM_OR_MINOR, SYNTH_CHORD_MOD_AUGMENTED},
};

const char *synth_key_name(synth_key_t key)
{
    return (unsigned)key < SYNTH_KEY_COUNT ? KEY_NAMES[key] : "?";
}
const char *synth_bass_mode_name(synth_bass_mode_t mode)
{
    return (unsigned)mode < SYNTH_BASS_MODE_COUNT ? BASS_NAMES[mode] : "?";
}
const char *synth_joystick_mode_name(synth_joystick_mode_t mode)
{
    return (unsigned)mode < SYNTH_JOYSTICK_MODE_COUNT ? MODE_NAMES[mode] : "?";
}
const char *synth_chord_modifier_name(synth_chord_modifier_t modifier)
{
    return (unsigned)modifier < SYNTH_CHORD_MOD_COUNT ? MODIFIERS[modifier].name : "?";
}

static int wrap_step(int value, int steps, int count)
{
    int result = (value % count + steps) % count;
    return result < 0 ? result + count : result;
}
synth_key_t synth_key_step(synth_key_t key, int8_t semitones)
{
    return (synth_key_t)wrap_step((int)key, semitones, SYNTH_KEY_COUNT);
}
synth_bass_mode_t synth_bass_mode_step(synth_bass_mode_t mode, int8_t steps)
{
    return (synth_bass_mode_t)wrap_step((int)mode, steps, SYNTH_BASS_MODE_COUNT);
}
synth_joystick_mode_t synth_joystick_mode_step(synth_joystick_mode_t mode, int8_t steps)
{
    return (synth_joystick_mode_t)wrap_step((int)mode, steps, SYNTH_JOYSTICK_MODE_COUNT);
}
synth_chord_modifier_t synth_chord_modifier_from_joystick(synth_joystick_mode_t mode,
                                                         control_joystick_direction_t direction)
{
    if ((unsigned)mode >= SYNTH_JOYSTICK_MODE_COUNT || (unsigned)direction > CONTROL_JOYSTICK_UP_LEFT) {
        return SYNTH_CHORD_MOD_NONE;
    }
    return JOYSTICK_MAP[mode][direction];
}
int8_t synth_chord_key_shift(synth_joystick_mode_t mode, control_joystick_direction_t direction)
{
    if (mode != SYNTH_JOYSTICK_MODE_CHROMATIC) {
        return 0;
    }
    return direction == CONTROL_JOYSTICK_RIGHT ? 1 : direction == CONTROL_JOYSTICK_LEFT ? -1 : 0;
}

static int abs_int(int value) { return value < 0 ? -value : value; }

static void invert(int16_t *notes, uint8_t count, uint8_t inversion)
{
    for (uint8_t step = 0; step < inversion; ++step) {
        uint8_t lowest = 0;
        for (uint8_t i = 1; i < count; ++i) {
            if (notes[i] < notes[lowest]) lowest = i;
        }
        notes[lowest] += 12;
    }
}

static void sort_notes(int16_t *notes, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i) {
        int16_t value = notes[i];
        uint8_t j = i;
        while (j > 0 && notes[j - 1] > value) {
            notes[j] = notes[j - 1];
            --j;
        }
        notes[j] = value;
    }
}

static bool notes_in_range(const int16_t *notes, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (notes[i] < 0 || notes[i] > 127) return false;
    }
    return true;
}

/* A deterministic bounded search, not a claim about closed-source HiChord
 * tie-breaking. Preserve role order in output; compare sorted pitch positions. */
static void voice_lead(int16_t *notes, uint8_t count, const synth_chord_request_t *request)
{
    int16_t previous[SYNTH_MAX_CHORD_NOTES];
    for (uint8_t i = 0; i < request->previous_note_count; ++i) previous[i] = request->previous_notes[i];
    sort_notes(previous, request->previous_note_count);
    int best_score = INT_MAX;
    int16_t best[6];
    memcpy(best, notes, count * sizeof(*notes));
    /* Prefer the requested inversion/register when scores tie. */
    static const int8_t OCTAVES[] = {0, -1, 1};
    for (uint8_t n = 0; n < SYNTH_CHORD_INVERSION_COUNT; ++n) {
        uint8_t inversion = (request->inversion + n) % SYNTH_CHORD_INVERSION_COUNT;
        for (uint8_t o = 0; o < 3; ++o) {
            int16_t candidate[6], sorted[6];
            for (uint8_t i = 0; i < count; ++i) candidate[i] = notes[i] + OCTAVES[o] * 12;
            invert(candidate, count, inversion);
            if (!notes_in_range(candidate, count)) continue;
            memcpy(sorted, candidate, count * sizeof(*notes));
            sort_notes(sorted, count);
            int sum = 0, previous_sum = 0, score = 0;
            for (uint8_t i = 0; i < count; ++i) sum += sorted[i];
            for (uint8_t i = 0; i < request->previous_note_count; ++i) previous_sum += previous[i];
            score = 2 * abs_int(sum / count - previous_sum / request->previous_note_count);
            uint8_t common = count < request->previous_note_count ? count : request->previous_note_count;
            for (uint8_t i = 0; i < common; ++i) score += 5 * abs_int(sorted[i] - previous[i]);
            if (score < best_score) {
                best_score = score;
                memcpy(best, candidate, count * sizeof(*notes));
            }
        }
    }
    memcpy(notes, best, count * sizeof(*notes));
}

/* Fit the voicing as a unit, never fold individual extensions below their root. */
static void fit_register(int16_t *notes, uint8_t count)
{
    int16_t low = notes[0], high = notes[0];
    for (uint8_t i = 1; i < count; ++i) {
        if (notes[i] < low) low = notes[i];
        if (notes[i] > high) high = notes[i];
    }
    int shift = 0;
    while (high + shift > 127) shift -= 12;
    while (low + shift < 0) shift += 12;
    for (uint8_t i = 0; i < count; ++i) notes[i] += shift;
}

static void append_name(char name[16], const char *part)
{
    size_t used = 0;
    while (used < 15 && name[used]) ++used;
    while (used < 15 && *part) name[used++] = *part++;
    name[used] = 0;
}

int synth_build_chord(const synth_chord_request_t *request, synth_chord_t *chord)
{
    if (request == NULL || chord == NULL ||
        (unsigned)request->key >= SYNTH_KEY_COUNT ||
        request->degree == 0 || request->degree > SYNTH_CHORD_BUTTON_COUNT ||
        (unsigned)request->modifier >= SYNTH_CHORD_MOD_COUNT ||
        (unsigned)request->bass_mode >= SYNTH_BASS_MODE_COUNT ||
        request->inversion < 0 || request->inversion >= SYNTH_CHORD_INVERSION_COUNT ||
        request->previous_note_count > SYNTH_MAX_CHORD_NOTES ||
        (request->bass_mode == SYNTH_BASS_MODE_SLASH &&
         (request->bass_degree == 0 || request->bass_degree > SYNTH_CHORD_BUTTON_COUNT))) {
        return -1;
    }
    for (uint8_t i = 0; i < request->previous_note_count; ++i) {
        if (request->previous_notes[i] > 127) return -1;
    }
    const modifier_definition_t *modifier = &MODIFIERS[request->modifier];
    const chord_formula_t *formula = &FORMULAS[modifier->family[DEGREE_FAMILIES[request->degree - 1]]];
    int root = SYNTH_DEFAULT_ROOT_MIDI + (int)request->key + DEGREE_ROOTS[request->degree - 1] +
               12 * ((int)request->global_octave_shift + request->button_octave_shift) + modifier->root_shift;
    if (root < 0 || root > 127) return -1;

    synth_chord_t result = {
        .degree = request->degree, .key = request->key, .root_midi = (uint8_t)root,
        .modifier = request->modifier, .inversion = request->inversion,
        .bass_mode = request->bass_mode, .bass_degree = request->bass_degree,
        .voice_leading_enabled = request->voice_leading_enabled, .note_count = formula->count,
    };
    int16_t notes[6] = {0};
    for (uint8_t i = 0; i < formula->count; ++i) notes[i] = root + formula->intervals[i];
    fit_register(notes, formula->count);
    if (request->voice_leading_enabled && request->previous_note_count > 0) {
        voice_lead(notes, formula->count, request);
    } else {
        invert(notes, formula->count, request->inversion);
        fit_register(notes, formula->count);
    }
    uint8_t lowest = 127;
    for (uint8_t i = 0; i < formula->count; ++i) {
        result.notes[i] = (uint8_t)notes[i];
        if (result.notes[i] < lowest) lowest = result.notes[i];
    }
    if (request->bass_mode != SYNTH_BASS_MODE_OFF) {
        int bass = root - 24;
        if (request->bass_mode == SYNTH_BASS_MODE_SLASH) {
            bass = SYNTH_DEFAULT_ROOT_MIDI + (int)request->key + DEGREE_ROOTS[request->bass_degree - 1] +
                   12 * ((int)request->global_octave_shift + request->bass_octave_shift);
            if (bass < 0 || bass > 127) return -1;
        }
        while (bass >= lowest) bass -= 12;
        while (bass < 0) bass += 12;
        if (bass >= lowest) return -1; /* No representable bass below this voicing. */
        result.bass_enabled = true;
        result.bass_note = (uint8_t)bass;
    }
    append_name(result.name, KEY_NAMES[root % 12]);
    append_name(result.name, formula->suffix);
    if (result.bass_mode == SYNTH_BASS_MODE_SLASH && result.bass_note % 12 != root % 12) {
        append_name(result.name, "/");
        append_name(result.name, KEY_NAMES[result.bass_note % 12]);
    }
    *chord = result;
    return 0;
}
