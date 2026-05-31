#include "synth/app/chords.h"

#include <string.h>

void synth_app_chord_state_remember_voice_anchor(synth_app_chord_state_t *state,
                                                 const synth_chord_t *chord)
{
    if (state == NULL || chord == NULL || chord->note_count == 0) {
        return;
    }
    state->voice_anchor = *chord;
    state->voice_anchor_valid = true;
}

void synth_app_chord_state_clear_voice_anchor(synth_app_chord_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(&state->voice_anchor, 0, sizeof(state->voice_anchor));
    state->voice_anchor_valid = false;
}

uint8_t synth_app_chord_state_first_valid_slot(const synth_app_chord_state_t *state,
                                               const bool valid[SYNTH_CHORD_BUTTON_COUNT])
{
    if (state == NULL || valid == NULL) {
        return UINT8_MAX;
    }

    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((state->held_mask & (1U << slot)) != 0 && valid[slot]) {
            return slot;
        }
    }
    return UINT8_MAX;
}

uint8_t synth_app_chord_state_active_valid_slot(const synth_app_chord_state_t *state,
                                                const bool valid[SYNTH_CHORD_BUTTON_COUNT])
{
    if (state == NULL || valid == NULL) {
        return UINT8_MAX;
    }
    if ((state->held_mask & (1U << state->focus_slot)) != 0 && valid[state->focus_slot]) {
        return state->focus_slot;
    }
    return synth_app_chord_state_first_valid_slot(state, valid);
}

uint8_t synth_app_chord_state_focused_slot(synth_app_chord_state_t *state)
{
    if (state == NULL) {
        return UINT8_MAX;
    }

    uint8_t slot = state->focus_slot;
    if ((state->held_mask & (1U << slot)) != 0) {
        return slot;
    }

    slot = synth_app_chord_state_active_valid_slot(state, state->held_chord_valid);
    if (slot < SYNTH_CHORD_BUTTON_COUNT) {
        state->focus_slot = slot;
    }
    return slot;
}

void synth_app_chord_state_clear_held(synth_app_chord_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->held_mask = 0;
    memset(state->held_chord_valid, 0, sizeof(state->held_chord_valid));
}
