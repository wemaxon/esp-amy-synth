#ifndef SYNTH_APP_TEMPO_H
#define SYNTH_APP_TEMPO_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_TEMPO_DEFAULT_BPM 120
#define SYNTH_APP_TEMPO_MIN_BPM 40
#define SYNTH_APP_TEMPO_MAX_BPM 300

#define SYNTH_APP_TEMPO_INITIALIZER \
    { \
        .bpm = SYNTH_APP_TEMPO_DEFAULT_BPM, \
    }

typedef struct {
    uint16_t bpm;
    uint8_t tap_count;
    TickType_t tap_first_tick;
    TickType_t tap_last_tick;
} synth_app_tempo_t;

typedef struct {
    bool changed;
    uint16_t bpm;
    uint8_t tap_count;
} synth_app_tempo_tap_result_t;

uint16_t synth_app_tempo_bpm(const synth_app_tempo_t *tempo);
bool synth_app_tempo_step(synth_app_tempo_t *tempo, int16_t delta);
synth_app_tempo_tap_result_t synth_app_tempo_tap(synth_app_tempo_t *tempo, TickType_t now);

#ifdef __cplusplus
}
#endif

#endif
