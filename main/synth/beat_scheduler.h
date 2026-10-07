#ifndef SYNTH_BEAT_SCHEDULER_H
#define SYNTH_BEAT_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "synth/drums.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sample-accurate drum loop playback. The application publishes what should
 * play (pattern, kit and the grid: origin frame + tempo); the audio task asks,
 * right before rendering each block, for the hits inside that block and sends
 * them with their sample offset. Frames are the looper's audio-frame clock, so
 * the beat and recorded loops share one grid.
 */
typedef struct {
    bool running;
    synth_drum_kit_t kit;
    synth_drum_loop_style_t style;
    synth_drum_loop_variation_t variation;
    uint64_t origin_frame; /* audio frame of step 0 */
    uint16_t bpm;
    /* Changes on every (re)start. A new restart may play its first steps up to
     * a short grace late; other changes continue exactly where scheduling was. */
    uint32_t restart_id;
} synth_beat_config_t;

/* Application task (single writer); lock-free. */
void synth_beat_publish(const synth_beat_config_t *config);
/* Audio task, before rendering the block starting at block_start_frame. */
void synth_beat_scheduler_render_block(uint64_t block_start_frame, uint32_t frames, uint32_t sample_rate);
void synth_beat_scheduler_reset(void);

/* Pure: sixteenth steps of the grid whose frame lies in [from_frame, to_frame),
 * in order, at most max. Uses the shared rational transport timing. */
uint32_t synth_beat_steps_in_range(uint64_t origin_frame,
                                   uint16_t bpm,
                                   uint32_t sample_rate,
                                   uint64_t from_frame,
                                   uint64_t to_frame,
                                   uint32_t *steps,
                                   uint64_t *frames,
                                   uint32_t max);

/* Provided by the drum layer: a hit starting sample_offset frames into the
 * next rendered block. */
esp_err_t synth_drums_hit_at(synth_drum_kit_t kit,
                             synth_drum_voice_t voice,
                             uint8_t velocity,
                             uint16_t sample_offset);

#ifdef __cplusplus
}
#endif

#endif
