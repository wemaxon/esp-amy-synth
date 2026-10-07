#include "synth/app/drum_transport.h"

#include <stddef.h>

#include "synth/transport_timing.h"

static uint64_t step_frame(uint64_t origin_frame, uint16_t bpm, uint32_t sample_rate, uint32_t step)
{
    return origin_frame + synth_transport_step_offset(sample_rate, bpm, step, 4);
}

/* First step of the grid at or after now (estimate with the rounded step
 * length, then settle exactly in both directions). */
static uint32_t next_step(uint64_t origin_frame, uint16_t bpm, uint32_t sample_rate, uint64_t now_frame)
{
    if (now_frame <= origin_frame) {
        return 0;
    }
    uint64_t step_frames = synth_transport_step_offset(sample_rate, bpm, 1, 4);
    uint32_t step = step_frames > 0 ? (uint32_t)((now_frame - origin_frame) / step_frames) : 0;
    while (step > 0 && step_frame(origin_frame, bpm, sample_rate, step - 1U) >= now_frame) {
        --step;
    }
    while (step_frame(origin_frame, bpm, sample_rate, step) < now_frame) {
        ++step;
    }
    return step;
}

void synth_app_drum_transport_start(synth_app_drum_transport_t *transport,
                                    synth_drum_loop_style_t style,
                                    uint16_t bpm,
                                    uint32_t sample_rate,
                                    uint64_t now_frame,
                                    const uint64_t *anchor_frame)
{
    (void)sample_rate;
    if (transport == NULL) {
        return;
    }

    transport->style = style;
    transport->running = true;
    transport->scheduled_bpm = bpm;
    transport->origin_frame = anchor_frame != NULL && *anchor_frame <= now_frame ? *anchor_frame : now_frame;
    ++transport->restart_id;
}

void synth_app_drum_transport_stop(synth_app_drum_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    transport->running = false;
    transport->scheduled_bpm = 0;
}

bool synth_app_drum_transport_update(synth_app_drum_transport_t *transport,
                                     uint16_t bpm,
                                     uint32_t sample_rate,
                                     uint64_t now_frame)
{
    if (transport == NULL || !transport->running || bpm == 0 || bpm == transport->scheduled_bpm) {
        return false;
    }

    uint16_t old_bpm = transport->scheduled_bpm;
    uint32_t step = next_step(transport->origin_frame, old_bpm, sample_rate, now_frame);
    uint64_t due = step_frame(transport->origin_frame, old_bpm, sample_rate, step);
    uint64_t offset = synth_transport_step_offset(sample_rate, bpm, step, 4);
    if (offset > due) {
        /* Keep the pattern position; restart the step count within the bar. */
        step %= SYNTH_DRUM_PATTERN_STEPS;
        offset = synth_transport_step_offset(sample_rate, bpm, step, 4);
    }
    transport->origin_frame = offset <= due ? due - offset : 0;
    transport->scheduled_bpm = bpm;
    return true;
}

void synth_app_drum_transport_config(const synth_app_drum_transport_t *transport,
                                     synth_beat_config_t *config)
{
    if (transport == NULL || config == NULL) {
        return;
    }
    *config = (synth_beat_config_t){
        .running = transport->running,
        .kit = transport->kit,
        .style = transport->style,
        .variation = transport->variation,
        .origin_frame = transport->origin_frame,
        .bpm = transport->scheduled_bpm,
        .restart_id = transport->restart_id,
    };
}
