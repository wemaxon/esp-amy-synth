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
    /* A lock stays in the key it was made in; locked_octave cancels the
     * global-octave carry of later B/C key steps so its pitch stays put. */
    synth_key_t locked_key[SYNTH_CHORD_BUTTON_COUNT];
    int8_t locked_octave[SYNTH_CHORD_BUTTON_COUNT];
    uint32_t pressed_tick[SYNTH_CHORD_BUTTON_COUNT];
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
/* Locks modifier in key, or unlocks when the slot already holds that modifier.
 * Returns true when the slot is locked afterwards. */
bool synth_app_chord_state_toggle_lock(synth_app_chord_state_t *state,
                                       uint8_t slot,
                                       synth_chord_modifier_t modifier,
                                       synth_key_t key);
/* Records when a chord button went down, in FreeRTOS ticks. */
void synth_app_chord_state_note_press(synth_app_chord_state_t *state, uint8_t slot, uint32_t now);
/* True when slot is held and was pressed at least min_ticks before now.
 * Per-button shortcuts require this so a near-simultaneous function press
 * is read as the function+chord combo instead. */
bool synth_app_chord_state_held_since(const synth_app_chord_state_t *state,
                                      uint8_t slot,
                                      uint32_t now,
                                      uint32_t min_ticks);
/* Call after a key step with the global octave it carried (-1, 0 or +1). */
void synth_app_chord_state_pin_locks(synth_app_chord_state_t *state, int8_t key_octave_carry);

#ifdef __cplusplus
}
#endif

#endif
