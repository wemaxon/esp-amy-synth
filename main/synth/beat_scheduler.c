#include "synth/beat_scheduler.h"

#include <stdatomic.h>
#include <stddef.h>

#include "synth/transport_timing.h"

#define DRUM_BIT(voice) ((uint8_t)(1U << (uint8_t)(voice)))
#define MAX_STEPS_PER_BLOCK 8U

/* How late a freshly started beat may play its first steps (the app runs up to
 * one app tick plus one audio block behind the audio clock). */
static const uint32_t RESTART_GRACE_MS = 40;

/* Seqlock: odd while the application is writing. */
static _Atomic uint32_t s_sequence;
static synth_beat_config_t s_published;

/* Audio-task state. */
static synth_beat_config_t s_config;
static bool s_scheduled_valid;
static uint64_t s_scheduled_until;

void synth_beat_publish(const synth_beat_config_t *config)
{
    if (config == NULL) {
        return;
    }
    atomic_fetch_add_explicit(&s_sequence, 1U, memory_order_acq_rel);
    atomic_thread_fence(memory_order_release);
    s_published = *config;
    atomic_thread_fence(memory_order_release);
    atomic_fetch_add_explicit(&s_sequence, 1U, memory_order_acq_rel);
}

static bool read_published(synth_beat_config_t *out)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t before = atomic_load_explicit(&s_sequence, memory_order_acquire);
        if ((before & 1U) != 0) {
            continue;
        }
        atomic_thread_fence(memory_order_acquire);
        synth_beat_config_t copy = s_published;
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&s_sequence, memory_order_acquire) == before) {
            *out = copy;
            return true;
        }
    }
    return false; /* writer busy: keep the previous configuration this block */
}

void synth_beat_scheduler_reset(void)
{
    s_config = (synth_beat_config_t){0};
    s_scheduled_valid = false;
    s_scheduled_until = 0;
    synth_beat_config_t idle = {0};
    synth_beat_publish(&idle);
}

static uint64_t step_frame(uint64_t origin_frame, uint16_t bpm, uint32_t sample_rate, uint32_t step)
{
    return origin_frame + synth_transport_step_offset(sample_rate, bpm, step, 4);
}

uint32_t synth_beat_steps_in_range(uint64_t origin_frame,
                                   uint16_t bpm,
                                   uint32_t sample_rate,
                                   uint64_t from_frame,
                                   uint64_t to_frame,
                                   uint32_t *steps,
                                   uint64_t *frames,
                                   uint32_t max)
{
    if (bpm == 0 || sample_rate == 0 || steps == NULL || frames == NULL || to_frame <= from_frame) {
        return 0;
    }
    if (from_frame < origin_frame) {
        from_frame = origin_frame;
    }
    if (to_frame <= from_frame) {
        return 0;
    }
    /* Estimate with the rounded step length, then settle exactly. */
    uint64_t step_frames = synth_transport_step_offset(sample_rate, bpm, 1, 4);
    uint32_t step = step_frames > 0 ? (uint32_t)((from_frame - origin_frame) / step_frames) : 0;
    while (step > 0 && step_frame(origin_frame, bpm, sample_rate, step - 1U) >= from_frame) {
        --step;
    }
    while (step_frame(origin_frame, bpm, sample_rate, step) < from_frame) {
        ++step;
    }
    uint32_t count = 0;
    for (uint64_t frame = step_frame(origin_frame, bpm, sample_rate, step);
         frame < to_frame && count < max;
         frame = step_frame(origin_frame, bpm, sample_rate, ++step)) {
        steps[count] = step;
        frames[count] = frame;
        ++count;
    }
    return count;
}

static void play_step(const synth_beat_config_t *config, uint32_t step, uint16_t sample_offset)
{
    synth_drum_pattern_step_t pattern =
        synth_drum_pattern_step(config->style, config->variation, (uint8_t)(step % SYNTH_DRUM_PATTERN_STEPS));
    for (uint8_t voice = 0; voice < SYNTH_DRUM_VOICE_COUNT; ++voice) {
        uint8_t bit = DRUM_BIT(voice);
        if ((pattern.mask & bit) != 0) {
            synth_drums_hit_at(config->kit,
                               (synth_drum_voice_t)voice,
                               synth_drums_default_velocity((synth_drum_voice_t)voice,
                                                            (pattern.accent_mask & bit) != 0),
                               sample_offset);
        }
    }
}

void synth_beat_scheduler_render_block(uint64_t block_start_frame, uint32_t frames, uint32_t sample_rate)
{
    synth_beat_config_t next;
    if (read_published(&next)) {
        if (!s_config.running || next.restart_id != s_config.restart_id) {
            s_scheduled_valid = false;
        }
        s_config = next;
    }
    uint64_t block_end = block_start_frame + frames;
    if (!s_config.running || frames == 0) {
        s_scheduled_valid = false;
        return;
    }

    uint64_t from = s_scheduled_until;
    if (!s_scheduled_valid) {
        /* New start: allow its first steps to be slightly late, never earlier
         * than its origin. */
        uint64_t grace = ((uint64_t)RESTART_GRACE_MS * sample_rate) / 1000U;
        from = block_start_frame > grace ? block_start_frame - grace : 0;
    }
    if (from < s_config.origin_frame) {
        from = s_config.origin_frame;
    }

    uint32_t steps[MAX_STEPS_PER_BLOCK];
    uint64_t step_frames[MAX_STEPS_PER_BLOCK];
    uint32_t count = synth_beat_steps_in_range(s_config.origin_frame,
                                               s_config.bpm,
                                               sample_rate,
                                               from,
                                               block_end,
                                               steps,
                                               step_frames,
                                               MAX_STEPS_PER_BLOCK);
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t offset = step_frames[i] > block_start_frame ? step_frames[i] - block_start_frame : 0;
        play_step(&s_config, steps[i], (uint16_t)offset);
    }
    s_scheduled_until = block_end;
    s_scheduled_valid = true;
}
