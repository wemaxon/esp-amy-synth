#include <stdint.h>

#include "synth/looper_dsp.c"

int32_t host_vector_encode_positive(void)
{
    return synth_looper_dsp_encode_sample(40000);
}

int32_t host_vector_encode_negative(void)
{
    return synth_looper_dsp_encode_sample(-40000);
}

int32_t host_vector_gain_q8(void)
{
    return synth_looper_dsp_apply_gain_q8(1000, 192);
}

int32_t host_vector_percent_gain(void)
{
    return synth_looper_dsp_apply_percent_gain(1000, 85);
}

int32_t host_vector_bus_gain_six(void)
{
    return synth_looper_dsp_bus_gain_q8(6);
}

int32_t host_vector_bus_gain_one(void)
{
    return synth_looper_dsp_bus_gain_q8(1);
}

int32_t host_vector_bus_gain_clamped(void)
{
    return synth_looper_dsp_bus_gain_q8(99);
}

int32_t host_vector_peak_half(void)
{
    return synth_looper_dsp_peak_percent(16384);
}

int32_t host_vector_limit_threshold(void)
{
    return synth_looper_dsp_saturate(24576, NULL);
}

int32_t host_vector_limit_positive(void)
{
    return synth_looper_dsp_saturate(40000, NULL);
}

int32_t host_vector_limit_negative(void)
{
    return synth_looper_dsp_saturate(-40000, NULL);
}

int32_t host_vector_clip_counter(void)
{
    synth_looper_meter_t meter = {0};
    synth_looper_dsp_saturate(40000, &meter);
    synth_looper_dsp_saturate(-40000, &meter);
    return meter.clipped_samples;
}

int32_t host_vector_seam_boundary(void)
{
    int16_t samples[] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, -1000};
    synth_looper_dsp_condition_seam(samples, 8, 1000);
    return samples[0] - samples[7];
}

int32_t host_vector_seam_interior(void)
{
    int16_t samples[] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, -1000};
    synth_looper_dsp_condition_seam(samples, 8, 1000);
    return samples[2] + samples[5];
}

int32_t host_vector_short_seam_unchanged(void)
{
    int16_t samples[] = {100, 200, 300};
    synth_looper_dsp_condition_seam(samples, 3, 1000);
    return samples[0] + samples[1] + samples[2];
}
