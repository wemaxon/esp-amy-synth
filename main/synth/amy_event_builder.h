#ifndef SYNTH_AMY_EVENT_BUILDER_H
#define SYNTH_AMY_EVENT_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "amy.h"
#include "synth/amy_engine.h"
#include "synth/amy_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bus 0 (AMY's default) carries the musical synth: patch synths, raw voices
 * and the user effects. AMY routes a patch's global EQ/chorus phrase to its
 * synth's bus, so drums get their own bus and sound changes never recolor them. */
#define AMY_SYNTH_BUS 0
#define AMY_DRUM_BUS 1
#define AMY_BUS_COUNT 2

typedef struct {
    bool enabled;
    float cutoff_hz;
    float minimum_hz;
    float maximum_hz;
} amy_filter_controls_t;

typedef struct {
    float amplitude;
    float pan;
    float detune_semitones;
    bool tremolo_enabled;
    bool lfo_enabled;
    uint16_t portamento_ms;
    amy_filter_controls_t filter;
    const synth_amy_envelope_t *envelope;
} amy_raw_note_event_config_t;

void amy_event_build_raw_note(amy_event *event,
                              uint16_t osc,
                              uint8_t midi_note,
                              const amy_sound_runtime_t *runtime,
                              const amy_raw_note_event_config_t *config);
void amy_event_build_patch_note(amy_event *event,
                                uint8_t synth_id,
                                float midi_note,
                                const amy_sound_runtime_t *runtime,
                                uint16_t portamento_ms);
void amy_event_build_raw_note_off(amy_event *event, uint16_t osc);
void amy_event_build_patch_note_off(amy_event *event, uint8_t synth_id, uint8_t midi_note);
void amy_event_apply_filter(amy_event *event,
                            const amy_raw_profile_t *profile,
                            const amy_filter_controls_t *controls);
void amy_event_apply_envelope(amy_event *event, const synth_amy_envelope_t *envelope);
void amy_event_send_raw_control(const amy_event *control, uint16_t main_osc,
                                 uint16_t pair_osc, bool pair_active);
void amy_event_send_patch_control(const amy_event *control, uint8_t oscs_per_voice,
                                   bool pair_loaded);
void amy_event_build_drum_hit(amy_event *event,
                              uint16_t osc,
                              uint8_t pcm_preset,
                              uint8_t midi_note,
                              float velocity,
                              float amplitude);
/* Final-mix volume is per bus in AMY; master volume must reach every bus. */
void amy_event_send_volume(float volume);
/* Flat EQ and no chorus on the synth bus, before a patch applies its own. */
void amy_event_build_synth_bus_reset(amy_event *event);

#ifdef __cplusplus
}
#endif

#endif

