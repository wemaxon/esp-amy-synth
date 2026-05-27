#ifndef SYNTH_APP_CHORDS_H
#define SYNTH_APP_CHORDS_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/chord_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_CHORD_STATE_INITIALIZER \
    { \
        .focus_slot = 0, \
    }

typedef struct {
    synth_chord_modifier_t locked_modifier[SYNTH_CHORD_BUTTON_COUNT];
    int8_t chord_inversion[SYNTH_CHORD_BUTTON_COUNT];
    uint8_t held_mask;
    uint8_t focus_slot;
    synth_chord_t held_chords[SYNTH_CHORD_BUTTON_COUNT];
    bool held_chord_valid[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_t voice_anchor;
    bool voice_anchor_valid;
} synth_app_chord_state_t;

void synth_app_chord_state_remember_voice_anchor(synth_app_chord_state_t *state,
                                                 const synth_chord_t *chord);
void synth_app_chord_state_clear_voice_anchor(synth_app_chord_state_t *state);
uint8_t synth_app_chord_state_first_valid_slot(const synth_app_chord_state_t *state,
                                               const bool valid[SYNTH_CHORD_BUTTON_COUNT]);
uint8_t synth_app_chord_state_active_valid_slot(const synth_app_chord_state_t *state,
                                                const bool valid[SYNTH_CHORD_BUTTON_COUNT]);
uint8_t synth_app_chord_state_focused_slot(synth_app_chord_state_t *state);
void synth_app_chord_state_clear_held(synth_app_chord_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
