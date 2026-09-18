#include "synth/mic_sample_dsp.h"

#include <math.h>
#include <stddef.h>

static int16_t saturate_i16(int64_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

float synth_mic_gain_from_percent(uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    return 0.5f * powf(20.0f, (float)percent / 100.0f);
}

void synth_mic_gain_plan(uint8_t percent, float applied_analog_db, synth_mic_gain_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    plan->total_gain = synth_mic_gain_from_percent(percent);
    plan->analog_gain_db = applied_analog_db;
    plan->digital_gain = plan->total_gain / powf(10.0f, applied_analog_db / 20.0f);
}

uint32_t synth_mic_copy_input_channel(int16_t *destination,
                                      uint32_t capacity_frames,
                                      uint32_t write_frame,
                                      const int16_t *interleaved,
                                      uint32_t input_frames,
                                      uint8_t channels,
                                      uint8_t input_channel,
                                      int32_t digital_gain_q16,
                                      synth_mic_capture_meter_t *meter)
{
    if (destination == NULL || interleaved == NULL || meter == NULL ||
        write_frame >= capacity_frames || channels == 0 || input_channel >= channels) {
        return 0;
    }

    uint32_t available = capacity_frames - write_frame;
    uint32_t copied = input_frames < available ? input_frames : available;
    int32_t peak = meter->peak;
    uint32_t clipped = meter->clipped_samples;
    for (uint32_t i = 0; i < copied; ++i) {
        int64_t scaled = ((int64_t)interleaved[i * channels + input_channel] *
                          digital_gain_q16) >> 16;
        if (scaled > INT16_MAX || scaled < INT16_MIN) {
            ++clipped;
        }
        int16_t sample = saturate_i16(scaled);
        int32_t magnitude = sample < 0 ? -(int32_t)sample : sample;
        if (magnitude > peak) {
            peak = magnitude;
        }
        destination[write_frame + i] = sample;
    }
    meter->peak = (uint16_t)peak;
    meter->clipped_samples = clipped;
    return copied;
}
