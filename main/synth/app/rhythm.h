#ifndef SYNTH_APP_RHYTHM_H
#define SYNTH_APP_RHYTHM_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t synth_app_rhythm_interval_ms(synth_rhythm_rate_t rate,
                                      uint16_t bpm,
                                      uint32_t step_index);
TickType_t synth_app_rhythm_advance_deadline(TickType_t deadline,
                                             synth_rhythm_rate_t rate,
                                             uint16_t bpm,
                                             uint32_t *step_index,
                                             TickType_t now);
void synth_app_build_arp_step_chord(synth_arp_pattern_t pattern,
                                    uint32_t step_index,
                                    const synth_chord_t *source,
                                    synth_chord_t *step);
void synth_app_append_step_notes(synth_chord_t *dest, const synth_chord_t *step);

#ifdef __cplusplus
}
#endif

#endif
