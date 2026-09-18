#include <stdint.h>

#include "synth/pitch_detector.c"

int32_t host_vector_pitch_square_500hz(void)
{
    int16_t samples[512];
    for (uint32_t i = 0; i < 512; ++i) {
        samples[i] = (i % 16U) < 8U ? 10000 : -10000;
    }
    synth_pitch_result_t result = {0};
    synth_pitch_detect(samples, 512, 8000, &result);
    return result.pitched ? (int32_t)(result.frequency_hz + 0.5f) : 0;
}

int32_t host_vector_pitch_rejects_silence(void)
{
    int16_t samples[512] = {0};
    synth_pitch_result_t result = {0};
    synth_pitch_detect(samples, 512, 8000, &result);
    return result.pitched ? 0 : 1;
}
