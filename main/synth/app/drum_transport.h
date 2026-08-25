#ifndef SYNTH_APP_DRUM_TRANSPORT_H
#define SYNTH_APP_DRUM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "synth/drums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_DRUM_TRANSPORT_INITIALIZER \
    { \
        .kit = SYNTH_DRUM_KIT_TIGHT, \
        .style = SYNTH_DRUM_LOOP_ROCK, \
        .variation = SYNTH_DRUM_LOOP_VARIATION_ORIGINAL, \
    }

typedef struct {
    synth_drum_kit_t kit;
    synth_drum_loop_style_t style;
    synth_drum_loop_variation_t variation;
    bool running;
    uint32_t step_index;
    TickType_t next_tick;
} synth_app_drum_transport_t;

void synth_app_drum_transport_start(synth_app_drum_transport_t *transport,
                                    synth_drum_loop_style_t style,
                                    TickType_t now);
void synth_app_drum_transport_stop(synth_app_drum_transport_t *transport);
bool synth_app_drum_transport_update(synth_app_drum_transport_t *transport,
                                     uint16_t bpm,
                                     TickType_t now);

#ifdef __cplusplus
}
#endif

#endif
