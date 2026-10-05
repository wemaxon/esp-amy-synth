/* Characterization of production code, not a second chord implementation.
 * Named vectors quote the official HiChord Rev 2.9 manual. */
#include <stdio.h>
#include <string.h>
#include "synth/amy_runtime.h"
#include "synth/chord_engine.h"
#include "synth/amy_voice_map.h"
#include "synth/app/chord_tools.h"
#include "synth/app/chords.h"
#include "synth/app/key.h"

static int matches(const synth_chord_t *chord, const char *name,
                   const uint8_t *notes, uint8_t count)
{
    if (chord->note_count != count) return 0;
    for (uint8_t i = 0; i < count; ++i) if (chord->notes[i] != notes[i]) return 0;
    for (uint8_t i = 0; i < sizeof(chord->name); ++i) {
        if (chord->name[i] != name[i]) return 0;
        if (!name[i]) return 1;
    }
    return 0;
}

int host_vector_chord_major(void)
{
    synth_chord_request_t request = {.degree = 1};
    synth_chord_t chord;
    return synth_build_chord(&request, &chord) == 0 &&
           matches(&chord, "C", (uint8_t[]){60,64,67}, 3);
}
int host_vector_chord_add11(void)
{
    synth_chord_request_t request = {.degree = 2, .modifier = SYNTH_CHORD_MOD_ADD11};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Dmadd11", (uint8_t[]){62,65,69,79}, 4)) return 0;
    request.degree = 7;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Bdimadd11", (uint8_t[]){71,74,77,88}, 4);
}
int host_vector_chord_ninth(void)
{
    synth_chord_request_t request = {.degree = 1, .modifier = SYNTH_CHORD_MOD_NINTH};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Cmaj9", (uint8_t[]){60,64,67,71,74}, 5)) return 0;
    request.degree = 6;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Am9", (uint8_t[]){69,72,76,79,83}, 5)) return 0;
    request.modifier = SYNTH_CHORD_MOD_OG_NINTH;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Am9", (uint8_t[]){69,72,76,79,84}, 5)) return 0;
    /* Manual 2.9: "On the diminished chord it plays m7b5." */
    request.degree = 7;
    request.modifier = SYNTH_CHORD_MOD_NINTH;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Bm7b5", (uint8_t[]){71,74,77,81}, 4)) return 0;
    /* OG keeps the corrected name and the Rev 2.8 doubled-third notes. */
    request.modifier = SYNTH_CHORD_MOD_OG_NINTH;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Bm7b5", (uint8_t[]){71,74,77,81,86}, 5);
}
int host_vector_chord_dim_left(void)
{
    /* Manual 2.9: "Major and minor chords become diminished. The diminished
     * chord (button 7) becomes minor." C -> Cdim, Bdim -> Bm. */
    synth_chord_request_t request = {.degree = 1, .modifier = SYNTH_CHORD_MOD_DIM_OR_MINOR};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Cdim", (uint8_t[]){60,63,66}, 3)) return 0;
    request.degree = 2;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Ddim", (uint8_t[]){62,65,68}, 3)) return 0;
    request.degree = 7;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Bm", (uint8_t[]){71,74,78}, 3);
}
int host_vector_chord_scales(void)
{
    /* Seven-note scales stack diatonic thirds on each degree of the key. */
    static const struct { synth_scale_t scale; const char *names[7]; } cases[] = {
        {SYNTH_SCALE_MAJOR, {"C", "Dm", "Em", "F", "G", "Am", "Bdim"}},
        {SYNTH_SCALE_NATURAL_MINOR, {"Cm", "Ddim", "D#", "Fm", "Gm", "G#", "A#"}},
        {SYNTH_SCALE_HARMONIC_MINOR, {"Cm", "Ddim", "D#aug", "Fm", "G", "G#", "Bdim"}},
        {SYNTH_SCALE_MELODIC_MINOR, {"Cm", "Dm", "D#aug", "F", "G", "Adim", "Bdim"}},
        {SYNTH_SCALE_DORIAN, {"Cm", "Dm", "D#", "F", "Gm", "Adim", "A#"}},
        {SYNTH_SCALE_MIXOLYDIAN, {"C", "Dm", "Edim", "F", "Gm", "Am", "A#"}},
        {SYNTH_SCALE_LYDIAN, {"C", "D", "Em", "F#dim", "G", "Am", "Bm"}},
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        for (uint8_t degree = 1; degree <= 7; ++degree) {
            synth_chord_request_t request = {.degree = degree, .scale = cases[c].scale};
            synth_chord_t chord;
            if (synth_build_chord(&request, &chord) || strcmp(chord.name, cases[c].names[degree - 1]) != 0) {
                return 0;
            }
        }
    }
    synth_chord_request_t request = {.degree = 3, .scale = SYNTH_SCALE_HARMONIC_MINOR};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "D#aug", (uint8_t[]){63,67,71}, 3)) return 0;
    /* Augmented degrees follow the major column for joystick modifiers. */
    request.modifier = SYNTH_CHORD_MOD_DIM_OR_MINOR;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "D#dim", (uint8_t[]){63,66,69}, 3)) return 0;
    /* Public sources do not define pentatonic/blues button chords. */
    request.scale = SYNTH_SCALE_MAJOR_PENTATONIC;
    chord.note_count = 99;
    if (synth_build_chord(&request, &chord) != -1 || chord.note_count != 99) return 0;
    request.scale = SYNTH_SCALE_COUNT;
    if (synth_build_chord(&request, &chord) != -1) return 0;
    return synth_scale_step(SYNTH_SCALE_MELODIC_MINOR, 1) == SYNTH_SCALE_DORIAN &&
           synth_scale_step(SYNTH_SCALE_DORIAN, -1) == SYNTH_SCALE_MELODIC_MINOR &&
           synth_scale_step(SYNTH_SCALE_LYDIAN, 1) == SYNTH_SCALE_MAJOR &&
           synth_scale_step(SYNTH_SCALE_MAJOR, -1) == SYNTH_SCALE_LYDIAN &&
           strcmp(synth_scale_name(SYNTH_SCALE_HARMONIC_MINOR), "HARM MIN") == 0;
}
int host_vector_chord_borrow(void)
{
    synth_chord_request_t request = {.degree = 2, .modifier = SYNTH_CHORD_MOD_FLAT_MAJOR};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "C#", (uint8_t[]){61,65,68}, 3)) return 0;
    request.degree = 4;
    request.modifier = SYNTH_CHORD_MOD_PASSING_DIM7;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "F#dim7", (uint8_t[]){66,69,72,75}, 4)) return 0;
    request.degree = 5;
    request.modifier = SYNTH_CHORD_MOD_TRITONE_DOM7;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "C#7", (uint8_t[]){61,65,68,71}, 4)) return 0;
    request.degree = 1;
    request.modifier = SYNTH_CHORD_MOD_BACKDOOR_DOM7;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "A#7", (uint8_t[]){58,62,65,68}, 4);
}
int host_vector_chord_inversion(void)
{
    synth_chord_request_t request = {.degree = 1, .inversion = 1, .voice_leading_enabled = true};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "C", (uint8_t[]){72,64,67}, 3)) return 0;
    request.inversion = 2;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "C", (uint8_t[]){72,76,67}, 3);
}
/* Manual 2.9: V.Lead "picks the smoothest inversion, so notes stay close".
 * Only the three inversions in the button's own register are candidates. */
int host_vector_chord_voice_leading(void)
{
    synth_chord_request_t request = {.degree = 4, .voice_leading_enabled = true,
        .previous_notes = {60,64,67}, .previous_note_count = 3};
    synth_chord_t chord;
    /* C -> F keeps C on the bottom: 2nd inversion C4 F4 A4 (role order F A C). */
    if (synth_build_chord(&request, &chord) || !matches(&chord, "F", (uint8_t[]){65,69,72}, 3)) return 0;
    /* The button register is respected: Bdim is not moved below B4. */
    request.degree = 7;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Bdim", (uint8_t[]){71,74,77}, 3)) return 0;
    /* Per-button octave still applies with voice leading on. */
    request.button_octave_shift = -1;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Bdim", (uint8_t[]){59,62,65}, 3);
}

/* Repeated progressions must not walk the register (the old octave search
 * climbed ~1.3 octaves on I-IV-V loops and the circle of fifths). */
int host_vector_chord_voice_leading_no_drift(void)
{
    static const uint8_t progressions[][8] = {
        {1, 4, 5, 6, 2, 5, 1, 3}, {1, 4, 7, 3, 6, 2, 5, 1},
        {1, 7, 6, 5, 4, 3, 2, 1}, {1, 5, 6, 4, 1, 5, 6, 4},
    };
    for (size_t p = 0; p < sizeof(progressions) / sizeof(progressions[0]); ++p) {
        synth_chord_t previous = {0};
        for (unsigned pass = 0; pass < 8; ++pass) {
            for (unsigned step = 0; step < 8; ++step) {
                synth_chord_request_t request = {.degree = progressions[p][step],
                    .voice_leading_enabled = true, .previous_note_count = previous.note_count};
                memcpy(request.previous_notes, previous.notes, previous.note_count);
                synth_chord_request_t plain = {.degree = progressions[p][step]};
                synth_chord_t chord, root_position;
                if (synth_build_chord(&request, &chord) || synth_build_chord(&plain, &root_position)) return 0;
                /* Every note stays within the button's register: root position up
                 * to its 2nd inversion, never below the root, never an octave above it. */
                for (uint8_t i = 0; i < chord.note_count; ++i) {
                    if (chord.notes[i] < root_position.notes[0] ||
                        chord.notes[i] >= root_position.notes[0] + 24) return 0;
                }
                for (uint8_t i = 0; i < chord.note_count; ++i) {
                    if (chord.notes[i] % 12 != root_position.notes[i] % 12) return 0;
                }
                previous = chord;
            }
        }
    }
    return 1;
}
int host_vector_chord_bass(void)
{
    synth_chord_request_t request = {.degree = 6, .bass_mode = SYNTH_BASS_MODE_SLASH, .bass_degree = 1};
    synth_chord_t chord;
    if (synth_build_chord(&request, &chord) || !matches(&chord, "Am/C", (uint8_t[]){69,72,76}, 3) || chord.bass_note != 60) return 0;
    request.modifier = SYNTH_CHORD_MOD_FLAT_MAJOR;
    request.bass_mode = SYNTH_BASS_MODE_ROOT;
    return synth_build_chord(&request, &chord) == 0 && chord.root_midi == 68 && chord.bass_note == 44;
}
int host_vector_chord_voice_assignment(void)
{
    synth_chord_request_t request = {.degree = 1, .modifier = SYNTH_CHORD_MOD_MAJ13, .bass_mode = SYNTH_BASS_MODE_ROOT};
    synth_chord_t chord;
    amy_voice_assignment_t voices[6];
    if (synth_build_chord(&request, &chord)) return 0;
    amy_voice_map_build_assignment(&chord, voices);
    const uint8_t expected[] = {60,64,71,36,74,81};
    for (uint8_t i = 0; i < 6; ++i) if (!voices[i].enabled || voices[i].note != expected[i]) return 0;
    request.bass_mode = SYNTH_BASS_MODE_OFF;
    if (synth_build_chord(&request, &chord)) return 0;
    amy_voice_map_build_assignment(&chord, voices);
    for (uint8_t i = 0; i < 6; ++i) if (!voices[i].enabled || voices[i].note != chord.notes[i]) return 0;
    chord.note_count = 1;
    amy_voice_map_build_assignment(&chord, voices);
    return voices[0].enabled && !voices[1].enabled && !voices[5].enabled;
}
int host_vector_chord_register(void)
{
    synth_chord_request_t request = {.key = SYNTH_KEY_B, .degree = 7,
        .global_octave_shift = 2, .button_octave_shift = 1, .modifier = SYNTH_CHORD_MOD_MAJ13};
    synth_chord_t chord;
    /* Shift the whole voicing down, rather than wrapping just the 13th. */
    if (synth_build_chord(&request, &chord) ||
        !matches(&chord, "A#maj13", (uint8_t[]){106,110,113,117,120,127}, 6)) return 0;
    /* +2 global and +2 button exceed MIDI; the voicing folds down whole octaves. */
    request.button_octave_shift = 2;
    request.modifier = SYNTH_CHORD_MOD_NONE;
    return synth_build_chord(&request, &chord) == 0 &&
           matches(&chord, "A#dim", (uint8_t[]){118,121,124}, 3);
}
int host_vector_chord_invalid(void)
{
    synth_chord_request_t request = {.degree = 0};
    synth_chord_t chord = {.note_count = 99};
    if (synth_build_chord(&request, &chord) != -1 || chord.note_count != 99) return 0;
    request.degree = 1;
    request.modifier = (synth_chord_modifier_t)-1;
    if (synth_build_chord(&request, &chord) != -1 || chord.note_count != 99) return 0;
    request.modifier = SYNTH_CHORD_MOD_NONE;
    request.previous_note_count = 9;
    if (synth_build_chord(&request, &chord) != -1) return 0;
    request.previous_note_count = 0;
    request.bass_mode = SYNTH_BASS_MODE_SLASH;
    request.bass_degree = 0;
    return synth_build_chord(&request, &chord) == -1 && chord.note_count == 99 &&
           synth_build_chord(NULL, &chord) == -1 && synth_build_chord(&request, NULL) == -1;
}
int host_vector_chord_gestures(void)
{
    return synth_chord_key_shift(SYNTH_JOYSTICK_MODE_CHROMATIC, CONTROL_JOYSTICK_LEFT) == -1 &&
        synth_chord_key_shift(SYNTH_JOYSTICK_MODE_CHROMATIC, CONTROL_JOYSTICK_RIGHT) == 1 &&
        synth_chord_key_shift(SYNTH_JOYSTICK_MODE_DEFAULT, CONTROL_JOYSTICK_RIGHT) == 0 &&
        synth_chord_modifier_from_joystick(SYNTH_JOYSTICK_MODE_CHROMATIC, CONTROL_JOYSTICK_LEFT) == SYNTH_CHORD_MOD_NONE &&
        synth_chord_modifier_from_joystick(SYNTH_JOYSTICK_MODE_CHROMATIC, CONTROL_JOYSTICK_UP_RIGHT) == SYNTH_CHORD_MOD_DOM7_ALT &&
        synth_chord_modifier_from_joystick(SYNTH_JOYSTICK_MODE_BORROW, CONTROL_JOYSTICK_UP_LEFT) == SYNTH_CHORD_MOD_TRITONE_DOM7 &&
        synth_chord_modifier_from_joystick(SYNTH_JOYSTICK_MODE_DEFAULT, CONTROL_JOYSTICK_DOWN_RIGHT) == SYNTH_CHORD_MOD_NINTH &&
        synth_chord_modifier_from_joystick(SYNTH_JOYSTICK_MODE_OG, CONTROL_JOYSTICK_DOWN_RIGHT) == SYNTH_CHORD_MOD_OG_NINTH &&
        synth_joystick_mode_step(SYNTH_JOYSTICK_MODE_OG, 1) == SYNTH_JOYSTICK_MODE_DEFAULT &&
        synth_joystick_mode_step(SYNTH_JOYSTICK_MODE_DEFAULT, -1) == SYNTH_JOYSTICK_MODE_OG;
}


static int application_chords(void)
{
    int8_t octaves[7] = {0};
    synth_app_chord_state_t state = SYNTH_APP_CHORD_STATE_INITIALIZER;
    if (!synth_app_chord_state_toggle_lock(&state, 0, SYNTH_CHORD_MOD_NINTH, SYNTH_KEY_B)) return 0;
    synth_app_chord_build_context_t context = {.key = SYNTH_KEY_B,
        .button_octave_shift = octaves, .chord_inversion = state.chord_inversion,
        .locked_modifier = state.locked_modifier, .locked_key = state.locked_key,
        .locked_octave = state.locked_octave, .held_mask = 3, .focus_slot = 1,
        .modifier = SYNTH_CHORD_MOD_FLAT_MAJOR};
    synth_chord_t chord;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) ||
        !matches(&chord, "Bmaj9", (uint8_t[]){71,75,78,82,85}, 5)) return 0;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) ||
        !matches(&chord, "C", (uint8_t[]){72,76,79}, 3)) return 0;
    /* Manual 2.9: "A locked chord stays pinned to the key it was locked in."
     * A B->C key step carries the global octave; the lock keeps its pitch. */
    int8_t previous_octave = context.global_octave_shift;
    if (!synth_app_key_step(context.key, 0, 1, &context.key, &context.global_octave_shift)) return 0;
    synth_app_chord_state_pin_locks(&state, (int8_t)(context.global_octave_shift - previous_octave));
    context.modifier = SYNTH_CHORD_MOD_NONE;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) ||
        !matches(&chord, "Bmaj9", (uint8_t[]){71,75,78,82,85}, 5)) return 0;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) ||
        !matches(&chord, "Dm", (uint8_t[]){74,77,81}, 3)) return 0;
    /* A deliberate global octave change still moves locked buttons. */
    context.global_octave_shift = 2;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) || chord.notes[0] != 83) return 0;
    context.global_octave_shift = 1;
    /* Locking the same modifier again unlocks, and the button follows the key. */
    if (synth_app_chord_state_toggle_lock(&state, 0, SYNTH_CHORD_MOD_NINTH, context.key)) return 0;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) ||
        !matches(&chord, "C", (uint8_t[]){72,76,79}, 3)) return 0;
    context.bass_mode = SYNTH_BASS_MODE_SLASH;
    context.slash_bass_slot = 0;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) ||
        !matches(&chord, "Dm/C", (uint8_t[]){74,77,81}, 3) || chord.bass_note != 72) return 0;
    /* Releasing the designated bass leaves the held chord's own root. */
    context.held_mask = 2;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) || chord.bass_note != 62) return 0;
    /* The selected scale reaches the engine. */
    context.scale = SYNTH_SCALE_NATURAL_MINOR;
    context.bass_mode = SYNTH_BASS_MODE_OFF;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) || strcmp(chord.name, "Cm") != 0) return 0;
    return !synth_app_build_chord_for_degree(&context, 0, &chord) &&
           !synth_app_build_chord_for_degree(&context, 8, &chord) &&
           synth_app_button_octave_step(1, 1, &octaves[0]) && octaves[0] == 2 &&
           !synth_app_button_octave_step(2, 1, &octaves[0]);
}

/* Per-button shortcuts (octave, inversion, lock) need the chord held first;
 * a near-simultaneous function press is the function+chord combo instead. */
static int shortcut_hold(void)
{
    synth_app_chord_state_t state = SYNTH_APP_CHORD_STATE_INITIALIZER;
    synth_app_chord_state_note_press(&state, 2, UINT32_MAX - 50U);
    state.held_mask = 1U << 2;
    if (synth_app_chord_state_held_since(&state, 2, UINT32_MAX - 50U + 149U, 150)) return 0;
    /* Tick wrap-around is handled. */
    if (!synth_app_chord_state_held_since(&state, 2, 100U, 150)) return 0;
    /* A released or never-pressed button is not a shortcut source. */
    state.held_mask = 0;
    if (synth_app_chord_state_held_since(&state, 2, 100U, 150)) return 0;
    return !synth_app_chord_state_held_since(&state, 9, 100U, 150);
}

static int register_boundaries(void)
{
    for (unsigned key = 0; key < 12; ++key)
    for (uint8_t degree = 1; degree <= 7; ++degree)
    for (int8_t global = -2; global <= 2; ++global)
    for (int8_t button = -2; button <= 2; ++button)
    for (unsigned mod = 0; mod < SYNTH_CHORD_MOD_COUNT; ++mod) {
        synth_chord_request_t request = {.key = (synth_key_t)key, .degree = degree,
            .global_octave_shift = global, .button_octave_shift = button,
            .modifier = (synth_chord_modifier_t)mod, .inversion = 2,
            .voice_leading_enabled = true, .previous_notes = {0, 4, 7}, .previous_note_count = 3};
        synth_chord_t chord;
        if (synth_build_chord(&request, &chord)) return 0;
        for (uint8_t i = 0; i < chord.note_count; ++i) if (chord.notes[i] > 127) return 0;
        if (!memchr(chord.name, 0, sizeof(chord.name))) return 0;
        request.previous_note_count = 0;
        if (synth_build_chord(&request, &chord)) return 0;
        for (uint8_t i = 0; i < chord.note_count; ++i) if (chord.notes[i] > 127) return 0;
    }
    return 1;
}

static int matrix(void)
{
    /* Independent expected pitch-class vocabulary: major/minor/diminished.
     * Rows follow the public modifier enum, not private production tables. */
    static const uint16_t pitch_classes[][3] = {
        {0x091,0x089,0x049}, {0x089,0x091,0x089}, {0x491,0x491,0x491},
        {0x891,0x489,0x449}, {0x095,0x08d,0x04d}, {0x0a1,0x0a1,0x0a1},
        {0x291,0x085,0x085}, {0x049,0x049,0x089}, {0x111,0x111,0x111},
        {0x499,0x499,0x499}, {0x4a1,0x4a1,0x4a1}, {0x0b1,0x0a9,0x069},
        {0x449,0x449,0x449}, {0x495,0x495,0x495}, {0x4a9,0x4a9,0x4a9},
        {0x889,0x889,0x889}, {0xa95,0xa95,0xa95}, {0x295,0x295,0x295},
        {0x8d1,0x8d1,0x8d1}, {0x695,0x695,0x695}, {0x493,0x493,0x493},
        {0x45b,0x45b,0x45b}, {0x895,0x48d,0x449}, {0x891,0x489,0x449},
        {0x091,0x091,0x091}, {0x249,0x249,0x249}, {0x491,0x491,0x491},
        {0x491,0x491,0x491}, {0x489,0x891,0x891},
    };
    static const uint8_t families[] = {0,1,1,0,0,1,2};
    static const uint8_t roots[] = {0,2,4,5,7,9,11};
    static const int8_t shifts[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,1,-6,-2,0};
    if (sizeof(pitch_classes) / sizeof(pitch_classes[0]) != SYNTH_CHORD_MOD_COUNT) return 0;
    unsigned vectors = 0;
    for (unsigned key = 0; key < 12; ++key)
    for (uint8_t degree = 1; degree <= 7; ++degree)
    for (unsigned mod = 0; mod < SYNTH_CHORD_MOD_COUNT; ++mod)
    for (int8_t inversion = 0; inversion < 3; ++inversion)
    for (unsigned bass = 0; bass < SYNTH_BASS_MODE_COUNT; ++bass) {
        synth_chord_request_t request = {.key = (synth_key_t)key, .degree = degree,
            .modifier = (synth_chord_modifier_t)mod, .inversion = inversion,
            .bass_mode = (synth_bass_mode_t)bass, .bass_degree = 1};
        synth_chord_t chord;
        if (synth_build_chord(&request, &chord)) return 0;
        if (chord.root_midi != 60 + key + roots[degree - 1] + shifts[mod]) return 0;
        uint16_t mask = 0;
        uint8_t lowest = 127;
        for (uint8_t i = 0; i < chord.note_count; ++i) {
            if (chord.notes[i] > 127) return 0;
            int interval = (chord.notes[i] + 120 - chord.root_midi) % 12;
            mask |= (uint16_t)(1U << interval);
            if (chord.notes[i] < lowest) lowest = chord.notes[i];
        }
        if (mask != pitch_classes[mod][families[degree - 1]]) {
            printf("pitch-class mismatch key=%u degree=%u modifier=%u inversion=%d: %x\n",
                   key, degree, mod, inversion, mask);
            return 0;
        }
        if (bass && (!chord.bass_enabled || chord.bass_note >= lowest)) return 0;
        amy_voice_assignment_t voices[6];
        amy_voice_map_build_assignment(&chord, voices);
        if (bass && voices[3].note != chord.bass_note) return 0;
        if (chord.note_count == 6 && voices[5].note != chord.notes[5]) return 0;
        for (uint8_t i = 0; i < 6; ++i) if (!voices[i].enabled || voices[i].note > 127) return 0;
        ++vectors;
    }
    printf("PASS: %u chord/voice assignment matrix vectors\n", vectors);
    return 1;
}

int main(void)
{
    static const struct { const char *name; int (*run)(void); } tests[] = {
        {"major", host_vector_chord_major}, {"add11", host_vector_chord_add11},
        {"ninth", host_vector_chord_ninth}, {"borrow", host_vector_chord_borrow},
        {"inversion", host_vector_chord_inversion}, {"voice leading", host_vector_chord_voice_leading},
        {"bass", host_vector_chord_bass}, {"voice assignment", host_vector_chord_voice_assignment},
        {"register", host_vector_chord_register}, {"invalid", host_vector_chord_invalid},
        {"gestures", host_vector_chord_gestures}, {"matrix", matrix},
        {"default left", host_vector_chord_dim_left}, {"scales", host_vector_chord_scales},
        {"voice leading drift", host_vector_chord_voice_leading_no_drift},
        {"shortcut hold", shortcut_hold},
        {"application chords", application_chords}, {"register boundaries", register_boundaries},
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) { fprintf(stderr, "FAIL: chord %s\n", tests[i].name); return 1; }
    }
    puts("PASS: production chord engine and voice mapper");
    return 0;
}
