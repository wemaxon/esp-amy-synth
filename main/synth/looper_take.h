#ifndef SYNTH_LOOPER_TAKE_H
#define SYNTH_LOOPER_TAKE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One recording pass into a track buffer, treating the loop as a circle.
 *
 * - Latency: the sample captured at grid time p + latency is stored at loop
 *   position p, so audio played against the click the player hears (output
 *   buffering) and produced after the button press (input path) lands on the
 *   beat instead of leaving a gap at the start of the loop.
 * - Seam: the last seam frames of the loop crossfade (equal power) into the
 *   audio captured just before position 0. On a circle that audio is what
 *   leads into position 0, so the wrap is continuous by construction, and
 *   notes played slightly before the downbeat land at the end of the loop.
 *
 * capture() is called for every audio frame, also when no take is active,
 * to keep the pre-roll current. Pure and allocation-free; audio-thread safe.
 */

#define SYNTH_LOOPER_TAKE_SEAM_MAX_FRAMES 4096U

typedef enum {
    SYNTH_LOOPER_TAKE_IDLE = 0,
    SYNTH_LOOPER_TAKE_CAPTURING,
    SYNTH_LOOPER_TAKE_FULL,  /* open take reached the buffer capacity */
    SYNTH_LOOPER_TAKE_DONE,  /* body and seam complete; the take is closed */
} synth_looper_take_status_t;

typedef struct {
    uint32_t latency;
    uint32_t seam;
    int16_t ring[SYNTH_LOOPER_TAKE_SEAM_MAX_FRAMES];
    uint32_t ring_write;
    int16_t pre[SYNTH_LOOPER_TAKE_SEAM_MAX_FRAMES];
    bool pre_ready;
    bool active;
    int16_t *samples;
    uint32_t capacity;
    uint32_t length; /* 0 while open */
    int64_t position; /* loop position of the next captured sample */
} synth_looper_take_t;

void synth_looper_take_init(synth_looper_take_t *take, uint32_t latency_frames, uint32_t seam_frames);
/* Called at grid position 0. length 0 = open (free length, set later). */
void synth_looper_take_begin(synth_looper_take_t *take, int16_t *samples, uint32_t capacity, uint32_t length);
synth_looper_take_status_t synth_looper_take_capture(synth_looper_take_t *take, int16_t sample);
/* Closes an open take; audio captured beyond length is discarded. */
synth_looper_take_status_t synth_looper_take_set_length(synth_looper_take_t *take, uint32_t length);
void synth_looper_take_abort(synth_looper_take_t *take);
bool synth_looper_take_active(const synth_looper_take_t *take);
/* Frames of loop body stored so far. */
uint32_t synth_looper_take_recorded(const synth_looper_take_t *take);
/* Nearest whole multiple of unit (half rounds up), at least one unit and at
 * most the largest multiple that fits capacity. unit 0 returns raw. */
uint32_t synth_looper_take_snap_length(uint32_t raw, uint32_t unit, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
