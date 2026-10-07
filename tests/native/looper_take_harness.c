#include "test_check.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "synth/looper_take.h"

#define CAPACITY 4000U

static int16_t buffer[CAPACITY];

/* Feed captured samples t = 0.. into the take; source(t) is the live audio. */
static synth_looper_take_status_t run(synth_looper_take_t *take, uint32_t frames, int16_t (*source)(uint32_t),
                                      uint32_t first_frame)
{
    synth_looper_take_status_t status = SYNTH_LOOPER_TAKE_CAPTURING;
    for (uint32_t t = 0; t < frames; ++t) {
        status = synth_looper_take_capture(take, source(first_frame + t));
        if (status == SYNTH_LOOPER_TAKE_DONE) {
            break;
        }
    }
    return status;
}

static int16_t ramp(uint32_t t) { return (int16_t)(t % 30000U); }
static int16_t silence(uint32_t t) { (void)t; return 0; }
static int16_t sine(uint32_t t) { return (int16_t)(8000.0f * sinf((float)t * 0.0731f)); }

/* Capture lags the grid by the latency: what was played at grid time p + latency
 * lands at loop position p, so a chord played "on the click you hear" is on the beat. */
int32_t host_vector_take_latency_alignment(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 100, 10);
    memset(buffer, 0, sizeof(buffer));
    synth_looper_take_begin(&take, buffer, CAPACITY, 1000);
    run(&take, 100 + 1000, ramp, 0);
    /* position 0 holds grid frame 100; position 500 holds frame 600 */
    return buffer[0] == 100 && buffer[500] == 600 && buffer[980] == 1080;
}

/* The take keeps capturing after the grid length until the delayed body is
 * complete, then reports DONE. */
int32_t host_vector_take_completes_after_latency(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 100, 10);
    synth_looper_take_begin(&take, buffer, CAPACITY, 1000);
    for (uint32_t t = 0; t < 1099; ++t) {
        if (synth_looper_take_capture(&take, 1) == SYNTH_LOOPER_TAKE_DONE) return 0;
    }
    return synth_looper_take_capture(&take, 1) == SYNTH_LOOPER_TAKE_DONE && !synth_looper_take_active(&take);
}

/* The seam crossfades the loop end into the audio captured just before
 * position 0, so a continuous sound plays across the wrap without a jump. */
int32_t host_vector_take_seam_is_continuous(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 100, 200);
    /* Pre-roll: the source runs before the take starts. */
    for (uint32_t t = 0; t < 500; ++t) synth_looper_take_capture(&take, sine(t));
    synth_looper_take_begin(&take, buffer, CAPACITY, 1500);
    run(&take, 2000, sine, 500);
    /* Last loop sample must lead into position 0 the way the live audio did:
     * position 0 = sine(500 + 100); the sample before it in time = sine(599). */
    int32_t jump = buffer[0] - buffer[1499];
    int32_t natural = sine(600) - sine(599);
    if (jump - natural > 40 || natural - jump > 40) return 0;
    /* No level bump: a steady sine keeps its amplitude through the crossfade. */
    int32_t peak = 0;
    for (uint32_t i = 1300; i < 1500; ++i) {
        int32_t v = buffer[i] < 0 ? -buffer[i] : buffer[i];
        if (v > peak) peak = v;
    }
    return peak < 8000 * 115 / 100;
}

/* A note played slightly before the downbeat lands at the end of the loop. */
int32_t host_vector_take_early_note_lands_at_end(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 0, 200);
    for (uint32_t t = 0; t < 300; ++t) {
        synth_looper_take_capture(&take, t >= 280 ? 10000 : 0); /* hit 20 frames early */
    }
    synth_looper_take_begin(&take, buffer, CAPACITY, 1000);
    run(&take, 1000, silence, 0);
    /* the early hit is the last 20 frames before position 0 */
    return buffer[999] > 9000 && buffer[985] > 8000 && buffer[900] == 0;
}

/* Open takes (free length) learn their length later; capturing past it is
 * discarded and the seam still folds in the pre-roll. */
int32_t host_vector_take_open_length(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 0, 50);
    for (uint32_t t = 0; t < 100; ++t) synth_looper_take_capture(&take, 7);
    synth_looper_take_begin(&take, buffer, CAPACITY, 0);
    if (run(&take, 1200, ramp, 0) != SYNTH_LOOPER_TAKE_CAPTURING) return 0;
    if (synth_looper_take_recorded(&take) != 1200) return 0;
    /* Stop rounded down to 1000: done at once, seam folded toward the pre-roll. */
    if (synth_looper_take_set_length(&take, 1000) != SYNTH_LOOPER_TAKE_DONE) return 0;
    return buffer[0] == 0 && buffer[500] == 500 && buffer[999] < 30;
}

/* An open take that fills the buffer reports FULL so the looper can close it. */
int32_t host_vector_take_full(void)
{
    synth_looper_take_t take;
    synth_looper_take_init(&take, 0, 10);
    synth_looper_take_begin(&take, buffer, 100, 0);
    for (uint32_t t = 0; t < 99; ++t) {
        if (synth_looper_take_capture(&take, 1) != SYNTH_LOOPER_TAKE_CAPTURING) return 0;
    }
    return synth_looper_take_capture(&take, 1) == SYNTH_LOOPER_TAKE_FULL;
}

/* Track 1 lengths snap to the musical unit (bar or beat), half rounds up. */
int32_t host_vector_take_snap(void)
{
    return synth_looper_take_snap_length(1000, 400, 4000) == 1200 &&
           synth_looper_take_snap_length(999, 400, 4000) == 800 &&
           synth_looper_take_snap_length(150, 400, 4000) == 400 &&
           synth_looper_take_snap_length(3990, 400, 3900) == 3600 &&
           synth_looper_take_snap_length(500, 0, 4000) == 500;
}

int main(void)
{
    CHECK_VECTOR(host_vector_take_latency_alignment, 1);
    CHECK_VECTOR(host_vector_take_completes_after_latency, 1);
    CHECK_VECTOR(host_vector_take_seam_is_continuous, 1);
    CHECK_VECTOR(host_vector_take_early_note_lands_at_end, 1);
    CHECK_VECTOR(host_vector_take_open_length, 1);
    CHECK_VECTOR(host_vector_take_full, 1);
    CHECK_VECTOR(host_vector_take_snap, 1);
    return 0;
}
