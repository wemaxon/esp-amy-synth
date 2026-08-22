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
    return synth_looper_dsp_soft_limit(24576, NULL);
}

int32_t host_vector_limit_positive(void)
{
    return synth_looper_dsp_soft_limit(32767, NULL);
}

int32_t host_vector_limit_negative(void)
{
    return synth_looper_dsp_soft_limit(-40000, NULL);
}

int32_t host_vector_limited_counter(void)
{
    synth_looper_meter_t meter = {0};
    synth_looper_dsp_soft_limit(32767, &meter);
    synth_looper_dsp_soft_limit(-40000, &meter);
    return meter.limited_samples;
}

int32_t host_vector_clip_counter(void)
{
    synth_looper_meter_t meter = {0};
    synth_looper_dsp_soft_limit(32767, &meter);
    synth_looper_dsp_soft_limit(-40000, &meter);
    return meter.would_clip_samples;
}
