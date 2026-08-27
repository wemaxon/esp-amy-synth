#include "synth/looper_dsp.h"

#include <stddef.h>

static const uint16_t GAIN_Q8_UNITY = 256;
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

