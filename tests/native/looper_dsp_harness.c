#include "test_check.h"
#include <stdint.h>

#include "synth/looper_dsp.h"

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

int main(void)
{
    CHECK_VECTOR(host_vector_encode_positive, 32767);
    CHECK_VECTOR(host_vector_encode_negative, -32768);
    CHECK_VECTOR(host_vector_gain_q8, 750);
    CHECK_VECTOR(host_vector_percent_gain, 850);
    CHECK_VECTOR(host_vector_bus_gain_one, 256);
    CHECK_VECTOR(host_vector_bus_gain_six, 110);
    CHECK_VECTOR(host_vector_bus_gain_clamped, 110);
    CHECK_VECTOR(host_vector_peak_half, 50);
    CHECK_VECTOR(host_vector_limit_threshold, 24576);
    CHECK_VECTOR(host_vector_limit_positive, 32767);
    CHECK_VECTOR(host_vector_limit_negative, -32768);
    CHECK_VECTOR(host_vector_clip_counter, 2);
    return 0;
}
