#ifndef SYNTH_APP_CHORD_TOOLS_H
#define SYNTH_APP_CHORD_TOOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synth/chord_engine.h"

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    const int8_t *button_octave_shift;
    synth_chord_modifier_t modifier;
    uint8_t focus_slot;
    uint8_t held_mask;
    const synth_chord_modifier_t *locked_modifier;
    const synth_key_t *locked_key;     /* optional; NULL makes locks follow the key */
    const int8_t *locked_octave;
    synth_bass_mode_t bass_mode;
    uint8_t slash_bass_slot;
    const int8_t *chord_inversion;
    bool voice_leading_enabled;
    const synth_chord_t *voice_anchor;
    bool voice_anchor_valid;
} synth_app_chord_build_context_t;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t synth_app_first_held_slot_except(uint8_t held_mask, uint8_t excluded_slot);
uint8_t synth_app_bass_slot_for_chord(uint8_t chord_slot,
                                      uint8_t held_mask,
                                      synth_bass_mode_t bass_mode,
                                      uint8_t slash_bass_slot);
bool synth_app_build_chord_for_degree(const synth_app_chord_build_context_t *context,
                                      uint8_t degree,
                                      synth_chord_t *chord);
void synth_app_format_chord_notes(const synth_chord_t *chord, char *out, size_t out_size);
void synth_app_make_root_note_chord(const synth_chord_t *source, synth_chord_t *note);
bool synth_app_build_play_layer(const synth_chord_t held_chords[SYNTH_CHORD_BUTTON_COUNT],
                                const bool held_valid[SYNTH_CHORD_BUTTON_COUNT],
                                uint8_t held_mask,
                                synth_bass_mode_t bass_mode,
                                uint8_t slash_bass_slot,
                                synth_chord_t *layer);

#ifdef __cplusplus
}
#endif

#endif
