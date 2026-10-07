#include "test_check.h"
#include <stdint.h>

#include "synth/beat_scheduler.h"

#define RATE 44100U
#define BLOCK 256U
/* 120 BPM sixteenths fall on floor(n * 5512.5) frames from the origin. */

typedef struct {
    uint8_t step;
    uint16_t offset;
    uint64_t block_start;
} hit_t;

static hit_t hits[4096];
static unsigned hit_count;
static uint64_t current_block;

synth_drum_pattern_step_t synth_drum_pattern_step(synth_drum_loop_style_t style,
                                                  synth_drum_loop_variation_t variation,
                                                  uint8_t step)
{
    (void)style;
    (void)variation;
    (void)step;
    return (synth_drum_pattern_step_t){.mask = 1U, .accent_mask = 0};
}

uint8_t synth_drums_default_velocity(synth_drum_voice_t voice, bool accented)
{
    (void)voice;
    (void)accented;
    return 100;
}

static uint8_t pending_step;

esp_err_t synth_drums_hit_at(synth_drum_kit_t kit, synth_drum_voice_t voice, uint8_t velocity,
                             uint16_t sample_offset)
{
    (void)kit;
    (void)voice;
    (void)velocity;
    if (hit_count < sizeof(hits) / sizeof(hits[0])) {
        hits[hit_count++] = (hit_t){.step = pending_step, .offset = sample_offset, .block_start = current_block};
    }
    return ESP_OK;
}

/* Every step frame inside [from, to) is returned exactly once, in order. */
int32_t host_vector_steps_in_range(void)
{
    uint32_t steps[8];
    uint64_t frames[8];
    /* Steps 0..2 sit at 1000, 6512, 12025. */
    uint32_t n = synth_beat_steps_in_range(1000, 120, RATE, 6512, 12026, steps, frames, 8);
    if (n != 2 || steps[0] != 1 || frames[0] != 6512 || steps[1] != 2 || frames[1] != 12025) return 0;
    if (synth_beat_steps_in_range(1000, 120, RATE, 6513, 12025, steps, frames, 8) != 0) return 0;
    /* One hour later the grid is still exact: step 28800 at origin + 158760000. */
    n = synth_beat_steps_in_range(1000, 120, RATE, 1000 + 158760000ULL - 10, 1000 + 158760000ULL + 1, steps, frames, 8);
    return n == 1 && steps[0] == 28800 && frames[0] == 1000 + 158760000ULL;
}

static void run_blocks(uint64_t from_block_start, unsigned blocks)
{
    for (unsigned b = 0; b < blocks; ++b) {
        current_block = from_block_start + (uint64_t)b * BLOCK;
        synth_beat_scheduler_render_block(current_block, BLOCK, RATE);
    }
}

/* Hits land on their exact sample: block start + sample offset == step frame. */
int32_t host_vector_hits_are_sample_accurate(void)
{
    synth_beat_scheduler_reset();
    hit_count = 0;
    synth_beat_config_t config = {.running = true, .bpm = 120, .origin_frame = 2560, .restart_id = 1};
    synth_beat_publish(&config);
    run_blocks(2560, 400); /* ~2.3 s */
    if (hit_count < 18) return 0;
    for (unsigned i = 0; i < hit_count; ++i) {
        uint64_t expected = 2560 + (uint64_t)((double)i * 5512.5);
        if (hits[i].block_start + hits[i].offset != expected) return 0;
    }
    return 1;
}

/* A beat whose first step is already slightly in the past (the app lags the
 * audio clock) still plays it, at the start of the next block, not a step late. */
int32_t host_vector_late_start_not_dropped(void)
{
    synth_beat_scheduler_reset();
    hit_count = 0;
    synth_beat_config_t config = {.running = true, .bpm = 120, .origin_frame = 10000, .restart_id = 7};
    synth_beat_publish(&config);
    run_blocks(10240, 1); /* origin is 240 frames before this block */
    return hit_count == 1 && hits[0].offset == 0;
}

/* Republishing (tempo, pattern) never replays or skips a step. */
int32_t host_vector_republish_keeps_continuity(void)
{
    synth_beat_scheduler_reset();
    hit_count = 0;
    synth_beat_config_t config = {.running = true, .bpm = 120, .origin_frame = 0, .restart_id = 3};
    synth_beat_publish(&config);
    run_blocks(0, 30); /* steps 0 and 1 (0, 5512) */
    config.variation = SYNTH_DRUM_LOOP_VARIATION_FILLS; /* same restart id */
    synth_beat_publish(&config);
    run_blocks(30 * BLOCK, 30); /* next steps: 11025 */
    if (hit_count != 3) return 0;
    for (unsigned i = 1; i < hit_count; ++i) {
        if (hits[i].block_start + hits[i].offset <= hits[i - 1].block_start + hits[i - 1].offset) return 0;
    }
    /* Stopping silences the next block. */
    config.running = false;
    synth_beat_publish(&config);
    run_blocks(60 * BLOCK, 100);
    return hit_count == 3;
}

int main(void)
{
    CHECK_VECTOR(host_vector_steps_in_range, 1);
    CHECK_VECTOR(host_vector_hits_are_sample_accurate, 1);
    CHECK_VECTOR(host_vector_late_start_not_dropped, 1);
    CHECK_VECTOR(host_vector_republish_keeps_continuity, 1);
    return 0;
}
