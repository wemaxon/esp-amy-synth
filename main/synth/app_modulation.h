#ifndef SYNTH_APP_MODULATION_H
#define SYNTH_APP_MODULATION_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_MODULATION_INITIALIZER {0}

typedef struct {
    float phase;
    TickType_t last_tick;
} synth_app_modulation_t;

void synth_app_modulation_reset(synth_app_modulation_t *modulation, TickType_t now);
bool synth_app_lfo_pitch(synth_app_modulation_t *modulation,
                         uint8_t level,
                         TickType_t now,
                         float *pitch_semitones);
bool synth_app_tremolo_amount(synth_app_modulation_t *modulation,
                              uint8_t level,
                              uint16_t bpm,
                              TickType_t now,
                              float *amount);

#ifdef __cplusplus
}
#endif

#endif
