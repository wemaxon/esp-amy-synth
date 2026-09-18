#include <stdint.h>

#include "synth/mic_sample_dsp.c"

int32_t host_vector_mic_right_slot(void)
{
    const int16_t input[] = {1, 11, 2, 22};
    int16_t output[2] = {0};
    synth_mic_capture_meter_t meter = {0};
    uint32_t copied = synth_mic_copy_input_channel(output, 2, 0, input, 2, 2, 1, 65536, &meter);
    return copied == 2 ? output[0] * 100 + output[1] : -1;
}

int32_t host_vector_mic_channel_isolation(void)
{
    const int16_t input[] = {1, 10, 2, 20, 3, 30};
    int16_t output[3] = {0};
    synth_mic_capture_meter_t meter = {0};
    synth_mic_copy_input_channel(output, 3, 0, input, 3, 2, 1, 65536, &meter);
    return output[0] + output[1] + output[2];
}

int32_t host_vector_mic_capacity_clamp(void)
{
    const int16_t input[] = {7, 8};
    int16_t output[3] = {1, 2, 0};
    synth_mic_capture_meter_t meter = {0};
    uint32_t copied = synth_mic_copy_input_channel(output, 3, 2, input, 2, 1, 0, 65536, &meter);
    return (int32_t)(copied * 10U) + output[2];
}

int32_t host_vector_mic_fixed_gain(void)
{
    const int16_t input[] = {1000};
    int16_t output[1] = {0};
    synth_mic_capture_meter_t meter = {0};
    synth_mic_copy_input_channel(output, 1, 0, input, 1, 1, 0, 32768, &meter);
    return output[0];
}

int32_t host_vector_mic_saturation_meter(void)
{
    const int16_t input[] = {20000, -20000};
    int16_t output[2] = {0};
    synth_mic_capture_meter_t meter = {0};
    synth_mic_copy_input_channel(output, 2, 0, input, 2, 1, 0, 131072, &meter);
    if (output[0] != INT16_MAX || output[1] != INT16_MIN || meter.peak != 32768U) {
        return -1;
    }
    return (int32_t)meter.clipped_samples;
}
