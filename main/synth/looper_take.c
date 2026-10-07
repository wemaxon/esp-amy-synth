#include "synth/looper_take.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const float HALF_PI = 1.57079633f;

void synth_looper_take_init(synth_looper_take_t *take, uint32_t latency_frames, uint32_t seam_frames)
{
    if (take == NULL) {
        return;
    }
    memset(take, 0, sizeof(*take));
    take->latency = latency_frames;
    take->seam = seam_frames > SYNTH_LOOPER_TAKE_SEAM_MAX_FRAMES ? SYNTH_LOOPER_TAKE_SEAM_MAX_FRAMES : seam_frames;
}

void synth_looper_take_begin(synth_looper_take_t *take, int16_t *samples, uint32_t capacity, uint32_t length)
{
    if (take == NULL || samples == NULL || capacity == 0) {
        return;
    }
    take->active = true;
    take->samples = samples;
    take->capacity = capacity;
    take->length = length > capacity ? capacity : length;
    take->position = -(int64_t)take->latency;
    take->pre_ready = false;
}

void synth_looper_take_abort(synth_looper_take_t *take)
{
    if (take != NULL) {
        take->active = false;
    }
}

bool synth_looper_take_active(const synth_looper_take_t *take)
{
    return take != NULL && take->active;
}

uint32_t synth_looper_take_recorded(const synth_looper_take_t *take)
{
    if (take == NULL || !take->active || take->position <= 0) {
        return 0;
    }
    return (uint32_t)take->position;
}

static void snapshot_preroll(synth_looper_take_t *take)
{
    /* Oldest first: pre[seam - 1] is the frame just before position 0. */
    for (uint32_t i = 0; i < take->seam; ++i) {
        take->pre[i] = take->ring[(take->ring_write + i) % take->seam];
    }
    take->pre_ready = true;
}

static int16_t saturate(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)lrintf(value);
}

/* Equal-power crossfade of the loop end into the pre-roll, which is what
 * leads into position 0 when the loop wraps. */
static void fold_seam(synth_looper_take_t *take)
{
    uint32_t seam = take->seam;
    if (seam > take->length / 2U) {
        seam = take->length / 2U;
    }
    const int16_t *pre = take->pre + (take->seam - seam);
    int16_t *end = take->samples + (take->length - seam);
    for (uint32_t i = 0; i < seam; ++i) {
        float angle = HALF_PI * ((float)i + 0.5f) / (float)seam;
        end[i] = saturate((float)end[i] * cosf(angle) + (float)pre[i] * sinf(angle));
    }
}

static synth_looper_take_status_t finish(synth_looper_take_t *take)
{
    fold_seam(take);
    take->active = false;
    return SYNTH_LOOPER_TAKE_DONE;
}

synth_looper_take_status_t synth_looper_take_capture(synth_looper_take_t *take, int16_t sample)
{
    if (take == NULL) {
        return SYNTH_LOOPER_TAKE_IDLE;
    }
    synth_looper_take_status_t status = SYNTH_LOOPER_TAKE_IDLE;
    if (take->active) {
        if (take->position == 0 && !take->pre_ready) {
            snapshot_preroll(take);
        }
        uint32_t limit = take->length > 0 ? take->length : take->capacity;
        if (take->position >= 0 && take->position < (int64_t)limit) {
            take->samples[take->position] = sample;
        }
        ++take->position;
        status = SYNTH_LOOPER_TAKE_CAPTURING;
        if (take->length > 0 && take->position >= (int64_t)take->length) {
            status = finish(take);
        } else if (take->length == 0 && take->position >= (int64_t)take->capacity) {
            status = SYNTH_LOOPER_TAKE_FULL;
        }
    }
    if (take->seam > 0) {
        take->ring[take->ring_write] = sample;
        take->ring_write = (take->ring_write + 1U) % take->seam;
    }
    return status;
}

synth_looper_take_status_t synth_looper_take_set_length(synth_looper_take_t *take, uint32_t length)
{
    if (take == NULL || !take->active || length == 0) {
        return SYNTH_LOOPER_TAKE_IDLE;
    }
    take->length = length > take->capacity ? take->capacity : length;
    if (take->pre_ready && take->position >= (int64_t)take->length) {
        return finish(take);
    }
    return SYNTH_LOOPER_TAKE_CAPTURING;
}

uint32_t synth_looper_take_snap_length(uint32_t raw, uint32_t unit, uint32_t capacity)
{
    if (unit == 0) {
        return raw > capacity ? capacity : raw;
    }
    uint32_t units = (raw + unit / 2U) / unit;
    uint32_t max_units = capacity / unit;
    if (units < 1U) {
        units = 1U;
    }
    if (max_units > 0 && units > max_units) {
        units = max_units;
    }
    return units * unit;
}
