#include "synth/looper_dsp.h"

#include <stddef.h>

static const uint16_t GAIN_Q8_UNITY = 256;
static const uint32_t SEAM_SMOOTH_MS = 2;
static const uint32_t SMOOTH_Q15_ONE = 32768U;
static const uint16_t LOOP_BUS_GAIN_BY_TRACKS_Q8[SYNTH_LOOPER_TRACK_COUNT + 1] = {
    0,
    256,
    160,
    142,
    128,
    118,
    110,
};

static uint32_t magnitude_i32(int32_t sample)
{
    if (sample < 0) {
        return sample == INT32_MIN ? (uint32_t)INT32_MAX : (uint32_t)-sample;
    }
    return (uint32_t)sample;
}

static void increment_counter(uint16_t *counter)
{
    if (*counter < UINT16_MAX) {
        ++(*counter);
    }
}

int16_t synth_looper_dsp_encode_sample(int32_t sample)
{
    if (sample > INT16_MAX) {
        sample = INT16_MAX;
    } else if (sample < INT16_MIN) {
        sample = INT16_MIN;
    }
    return (int16_t)sample;
}

int32_t synth_looper_dsp_apply_gain_q8(int32_t sample, uint16_t gain_q8)
{
    if (gain_q8 == GAIN_Q8_UNITY) {
        return sample;
    }
    return (sample * (int32_t)gain_q8) / (int32_t)GAIN_Q8_UNITY;
}

int32_t synth_looper_dsp_apply_percent_gain(int32_t sample, uint8_t percent)
{
    if (percent >= 100) {
        return sample;
    }
    return (sample * (int32_t)percent) / 100;
}

uint16_t synth_looper_dsp_bus_gain_q8(uint8_t active_tracks)
{
    if (active_tracks > SYNTH_LOOPER_TRACK_COUNT) {
        active_tracks = SYNTH_LOOPER_TRACK_COUNT;
    }
    return LOOP_BUS_GAIN_BY_TRACKS_Q8[active_tracks];
}

void synth_looper_dsp_meter_peak(uint32_t *peak, int32_t sample)
{
    if (peak == NULL) {
        return;
    }
    uint32_t magnitude = magnitude_i32(sample);
    if (magnitude > *peak) {
        *peak = magnitude;
    }
}

uint8_t synth_looper_dsp_peak_percent(uint32_t peak)
{
    if (peak >= 32767U) {
        return 100;
    }
    return (uint8_t)(((peak * 100U) + 16383U) / 32767U);
}

int16_t synth_looper_dsp_saturate(int32_t sample, synth_looper_meter_t *meter)
{
    if (sample > INT16_MAX) {
        if (meter != NULL) {
            increment_counter(&meter->clipped_samples);
        }
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        if (meter != NULL) {
            increment_counter(&meter->clipped_samples);
        }
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static uint32_t smoothstep_q15(uint32_t position, uint32_t length)
{
    if (length <= 1U || position >= length - 1U) {
        return SMOOTH_Q15_ONE;
    }

    uint32_t t = (uint32_t)(((uint64_t)position * SMOOTH_Q15_ONE) /
                            (uint64_t)(length - 1U));
    uint32_t t2 = (uint32_t)(((uint64_t)t * t) >> 15);
    return (uint32_t)(((uint64_t)t2 *
                       ((3U * SMOOTH_Q15_ONE) - (2U * t))) >> 15);
}

static int16_t blend_sample(int32_t from, int32_t to, uint32_t amount_q15)
{
    int64_t mixed = (int64_t)from * (int64_t)(SMOOTH_Q15_ONE - amount_q15) +
                    (int64_t)to * (int64_t)amount_q15;
    return (int16_t)(mixed / (int64_t)SMOOTH_Q15_ONE);
}

void synth_looper_dsp_condition_seam(int16_t *samples,
                                     uint32_t length_frames,
                                     uint32_t sample_rate_hz)
{
    if (samples == NULL || sample_rate_hz == 0 || length_frames < 4U) {
        return;
    }

    uint32_t window = (uint32_t)(((uint64_t)sample_rate_hz * SEAM_SMOOTH_MS) / 1000ULL);
    uint32_t max_window = length_frames / 4U;
    if (window > max_window) {
        window = max_window;
    }
    if (window < 2U) {
        return;
    }

    int32_t midpoint = ((int32_t)samples[0] + (int32_t)samples[length_frames - 1U]) / 2;
    uint32_t tail_start = length_frames - window;
    for (uint32_t position = 0; position < window; ++position) {
        uint32_t amount = smoothstep_q15(position, window);
        uint32_t tail_index = tail_start + position;
        samples[position] = blend_sample(midpoint, samples[position], amount);
        samples[tail_index] = blend_sample(samples[tail_index], midpoint, amount);
    }
}

