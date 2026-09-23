#include "test_check.h"
#include <stdint.h>

#include "synth/pitch_detector.h"

int32_t host_vector_pitch_square_500hz(void)
{
    int16_t samples[512];
    for (uint32_t i = 0; i < 512; ++i) {
        samples[i] = (i % 16U) < 8U ? 10000 : -10000;
    }
    synth_pitch_result_t result = {0};
    synth_pitch_detect(samples, 512, 8000, &result);
    /* Period interpolation on a sampled square wave need not land exactly
     * on 500 Hz. Require <0.5% error, not one platform's rounded float. */
    return result.pitched && result.frequency_hz >= 497.5f && result.frequency_hz <= 502.5f;
}

int32_t host_vector_pitch_rejects_silence(void)
{
    int16_t samples[512] = {0};
    synth_pitch_result_t result = {0};
    synth_pitch_detect(samples, 512, 8000, &result);
    return result.pitched ? 0 : 1;
}

int main(void)
{
    CHECK_VECTOR(host_vector_pitch_square_500hz, 1);
    CHECK_VECTOR(host_vector_pitch_rejects_silence, 1);
    return 0;
}
