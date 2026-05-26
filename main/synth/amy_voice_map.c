#include "synth/amy_voice_map.h"

#include <stdio.h>
#include <string.h>

static const float VOICE_ROLE_AMPS[AMY_MUSICAL_VOICE_COUNT] = {
    0.90f, 0.64f, 0.64f, 0.34f, 0.52f, 0.28f,
};

static const char *const VOICE_ROLE_NAMES[AMY_MUSICAL_VOICE_COUNT] = {
    "root", "third", "fifth", "bass", "extension", "fill",
};

static const uint8_t RAW_ROLE_PRIORITY[AMY_MUSICAL_VOICE_COUNT] = {
    0, 1, 2, 3, 4, 5,
};

static const uint8_t PATCH_ROLE_PRIORITY[AMY_MUSICAL_VOICE_COUNT] = {
    0, 1, 2, 3, 4, 5,
};

static const float VOICE_MAIN_PANS[AMY_MUSICAL_VOICE_COUNT] = {
    0.20f, 0.80f, 0.35f, 0.65f, 0.45f, 0.55f,
};

static void format_midi_note(uint8_t midi_note, char *buffer, size_t buffer_size)
{
    static const char *const NOTE_NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%s%d",
             NOTE_NAMES[midi_note % 12],
             (int)(midi_note / 12) - 1);
}

static uint8_t octave_fill_note(uint8_t note)
{
    return note <= 115 ? (uint8_t)(note + 12U) : note;
}

static uint8_t chord_fill_note(const synth_chord_t *chord, uint8_t note_index)
{
    if (chord == NULL || chord->note_count == 0) {
        return SYNTH_DEFAULT_ROOT_MIDI;
    }

    if (note_index < chord->note_count) {
        return chord->notes[note_index];
    }

    return octave_fill_note(chord->notes[note_index % chord->note_count]);
}

static void assign_voice_note(amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT],
                              uint8_t role,
                              uint8_t note)
{
    if (role >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }

    assignments[role].enabled = true;
    assignments[role].note = note;
}

float amy_voice_map_role_amp(uint8_t role)
{
    return role < AMY_MUSICAL_VOICE_COUNT ? VOICE_ROLE_AMPS[role] : 0.0f;
}

float amy_voice_map_main_pan(uint8_t role)
{
    return role < AMY_MUSICAL_VOICE_COUNT ? VOICE_MAIN_PANS[role] : 0.5f;
}

const char *amy_voice_map_role_name(uint8_t role)
{
    return role < AMY_MUSICAL_VOICE_COUNT ? VOICE_ROLE_NAMES[role] : "?";
}

const uint8_t *amy_voice_map_role_priority(amy_sound_backend_t backend)
{
    if (backend == AMY_SOUND_BACKEND_PATCH) {
        return PATCH_ROLE_PRIORITY;
    }
    return RAW_ROLE_PRIORITY;
}

void amy_voice_map_build_assignment(const synth_chord_t *chord,
                                    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT])
{
    memset(assignments, 0, sizeof(amy_voice_assignment_t) * AMY_MUSICAL_VOICE_COUNT);
    if (chord == NULL || chord->note_count == 0) {
        return;
    }

    assign_voice_note(assignments, 0, chord->notes[0]);

    if (chord->note_count == 1) {
        return;
    }

    assign_voice_note(assignments, 1, chord->notes[1]);

    if (chord->note_count == 2) {
        return;
    }

    assign_voice_note(assignments, 2, chord->notes[2]);

    if (chord->bass_enabled) {
        assign_voice_note(assignments, 3, chord->bass_note);
        assign_voice_note(assignments, 4, chord_fill_note(chord, 3));
        assign_voice_note(assignments, 5, chord_fill_note(chord, 4));
        return;
    }

    assign_voice_note(assignments, 3, chord_fill_note(chord, 3));
    assign_voice_note(assignments, 4, chord_fill_note(chord, 4));
    assign_voice_note(assignments, 5, chord_fill_note(chord, 5));
}

void amy_voice_map_append_note_log(char *buffer,
                                   size_t buffer_size,
                                   size_t *used,
                                   const char *prefix,
                                   uint8_t role,
                                   uint8_t note)
{
    if (buffer == NULL || used == NULL || *used >= buffer_size || role >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }

    char note_name[8] = {0};
    format_midi_note(note, note_name, sizeof(note_name));
    int written = snprintf(buffer + *used,
                           buffer_size - *used,
                           "%s%s%s=%s(%u)",
                           *used == 0 ? "" : " ",
                           prefix,
                           amy_voice_map_role_name(role),
                           note_name,
                           note);
    if (written > 0) {
        size_t available = buffer_size - *used;
        *used += (size_t)written < available ? (size_t)written : available - 1U;
    }
}
