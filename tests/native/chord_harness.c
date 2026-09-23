/* Characterization of production code, not a second chord implementation. */
#include <string.h>
#include "synth/amy_runtime.h"
#include "synth/chord_engine.h"
#include "synth/amy_voice_map.h"
#include "synth/app/chord_tools.h"
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
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Am9", (uint8_t[]){69,72,76,79,84}, 5);
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
int host_vector_chord_voice_leading(void)
{
    synth_chord_request_t request = {.degree = 7, .voice_leading_enabled = true,
        .previous_notes = {60,64,67}, .previous_note_count = 3};
    synth_chord_t chord;
    return synth_build_chord(&request, &chord) == 0 && matches(&chord, "Bdim", (uint8_t[]){59,62,65}, 3);
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
    return synth_build_chord(&request, &chord) == 0 &&
           matches(&chord, "A#maj13", (uint8_t[]){106,110,113,117,120,127}, 6);
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

#include <stdio.h>

static int application_chords(void)
{
    int8_t octaves[7] = {0}, inversions[7] = {0};
    synth_chord_modifier_t locks[7] = {SYNTH_CHORD_MOD_NINTH};
    synth_app_chord_build_context_t context = {.key = SYNTH_KEY_B,
        .button_octave_shift = octaves, .chord_inversion = inversions,
        .locked_modifier = locks, .held_mask = 3, .focus_slot = 1,
        .modifier = SYNTH_CHORD_MOD_FLAT_MAJOR};
    synth_chord_t chord;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) ||
        !matches(&chord, "Bmaj9", (uint8_t[]){71,75,78,82,85}, 5)) return 0;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) ||
        !matches(&chord, "C", (uint8_t[]){72,76,79}, 3)) return 0;
    /* A key gesture transposes locked chords too, including the B/C carry. */
    if (!synth_app_key_step(context.key, 0, 1, &context.key, &context.global_octave_shift)) return 0;
    context.modifier = SYNTH_CHORD_MOD_NONE;
    if (!synth_app_build_chord_for_degree(&context, 1, &chord) ||
        !matches(&chord, "Cmaj9", (uint8_t[]){72,76,79,83,86}, 5)) return 0;
    context.bass_mode = SYNTH_BASS_MODE_SLASH;
    context.slash_bass_slot = 0;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) ||
        !matches(&chord, "Dm/C", (uint8_t[]){74,77,81}, 3) || chord.bass_note != 72) return 0;
    /* Releasing the designated bass leaves the held chord's own root. */
    context.held_mask = 2;
    if (!synth_app_build_chord_for_degree(&context, 2, &chord) || chord.bass_note != 62) return 0;
    return !synth_app_build_chord_for_degree(&context, 0, &chord) &&
           !synth_app_build_chord_for_degree(&context, 8, &chord);
}

static int register_boundaries(void)
{
    for (unsigned key = 0; key < 12; ++key)
    for (uint8_t degree = 1; degree <= 7; ++degree)
    for (int8_t global = -2; global <= 2; ++global)
    for (int8_t button = -2; button <= 1; ++button)
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
        {0x291,0x085,0x085}, {0x089,0x049,0x049}, {0x111,0x111,0x111},
        {0x499,0x499,0x499}, {0x4a1,0x4a1,0x4a1}, {0x0b1,0x0a9,0x069},
        {0x449,0x449,0x449}, {0x495,0x495,0x495}, {0x4a9,0x4a9,0x4a9},
        {0x889,0x889,0x889}, {0xa95,0xa95,0xa95}, {0x295,0x295,0x295},
        {0x8d1,0x8d1,0x8d1}, {0x695,0x695,0x695}, {0x493,0x493,0x493},
        {0x45b,0x45b,0x45b}, {0x895,0x48d,0x44d}, {0x891,0x489,0x449},
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
        {"application chords", application_chords}, {"register boundaries", register_boundaries},
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) { fprintf(stderr, "FAIL: chord %s\n", tests[i].name); return 1; }
    }
    puts("PASS: production chord engine and voice mapper");
    return 0;
}
