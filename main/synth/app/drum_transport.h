#ifndef SYNTH_APP_DRUM_TRANSPORT_H
#define SYNTH_APP_DRUM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/beat_scheduler.h"
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

/* The drum loop's grid on the audio frame clock (the looper's), so it shares
 * one grid with recorded loops. It plays live on the drum bus, never recorded.
 * This holds what should play; the audio-side beat scheduler plays the hits
 * sample-accurately (synth_app_drum_transport_config -> synth_beat_publish). */
typedef struct {
    synth_drum_kit_t kit;
    synth_drum_loop_style_t style;
    synth_drum_loop_variation_t variation;
    bool running;
    uint64_t origin_frame; /* audio frame of step 0 */
    uint16_t scheduled_bpm;
    uint32_t restart_id;
} synth_app_drum_transport_t;

/* anchor_frame NULL starts the pattern now. Otherwise the pattern sits on
 * that grid (a loop's position 0) and the first hit is its next step. */
void synth_app_drum_transport_start(synth_app_drum_transport_t *transport,
                                    synth_drum_loop_style_t style,
                                    uint16_t bpm,
                                    uint32_t sample_rate,
                                    uint64_t now_frame,
                                    const uint64_t *anchor_frame);
void synth_app_drum_transport_stop(synth_app_drum_transport_t *transport);
/* Follows tempo changes, keeping the next due step in place. Returns true
 * when the grid moved. */
bool synth_app_drum_transport_update(synth_app_drum_transport_t *transport,
                                     uint16_t bpm,
                                     uint32_t sample_rate,
                                     uint64_t now_frame);
void synth_app_drum_transport_config(const synth_app_drum_transport_t *transport,
                                     synth_beat_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
