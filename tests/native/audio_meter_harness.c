#include "test_check.h"
#include <stdint.h>

#include "synth/audio_meter.h"

static void fill_half_scale(int16_t *samples, uint32_t frames)
{
    for (uint32_t frame = 0; frame < frames; ++frame) {
        samples[frame * 2U] = 16384;
        samples[frame * 2U + 1U] = -16384;
    }
}

int32_t host_vector_audio_meter_peak(void)
{
    int16_t samples[512];
    fill_half_scale(samples, 256);
    if (synth_audio_meter_init(44100, 256, 5804) != ESP_OK) {
        return -1;
    }
    synth_audio_meter_request_t request = {
        .session_id = 7,
        .tap = SYNTH_AUDIO_METER_TAP_DRY,
        .target_frames = 256,
    };
    synth_audio_meter_start(&request);
    synth_audio_meter_begin_block();
    synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_DRY, samples, 256, 2);
    synth_audio_meter_result_t result = {0};
    synth_audio_meter_read(&result);
    return (int32_t)(result.sample_peak * 1000.0f);
}

int32_t host_vector_audio_meter_rms(void)
{
    int16_t samples[512];
    fill_half_scale(samples, 256);
    synth_audio_meter_init(44100, 256, 5804);
    synth_audio_meter_request_t request = {
        .session_id = 8,
        .tap = SYNTH_AUDIO_METER_TAP_DRY,
        .target_frames = 256,
    };
    synth_audio_meter_start(&request);
    synth_audio_meter_begin_block();
    synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_DRY, samples, 256, 2);
    synth_audio_meter_result_t result = {0};
    synth_audio_meter_read(&result);
    return (int32_t)(result.mean_square * 1000.0f);
}

int32_t host_vector_audio_meter_completion(void)
{
    int16_t samples[512];
    fill_half_scale(samples, 256);
    synth_audio_meter_init(44100, 256, 5804);
    synth_audio_meter_request_t request = {
        .session_id = 9,
        .tap = SYNTH_AUDIO_METER_TAP_OUTPUT,
        .target_frames = 256,
    };
    synth_audio_meter_start(&request);
    synth_audio_meter_begin_block();
    synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_DRY, samples, 256, 2);
    synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_OUTPUT, samples, 256, 2);
    synth_audio_meter_result_t result = {0};
    synth_audio_meter_read(&result);
    return (int32_t)(result.session_id * 100U + result.complete * 10U +
                     result.measured_frames / 256U);
}

int32_t host_vector_audio_meter_rejects_overlap(void)
{
    synth_audio_meter_init(44100, 256, 5804);
    synth_audio_meter_request_t request = {
        .session_id = 1,
        .tap = SYNTH_AUDIO_METER_TAP_DRY,
        .target_frames = 256,
    };
    if (synth_audio_meter_start(&request) != ESP_OK) {
        return -1;
    }
    return synth_audio_meter_start(&request);
}

int main(void)
{
    CHECK_VECTOR(host_vector_audio_meter_peak, 500);
    CHECK_VECTOR(host_vector_audio_meter_rms, 250);
    CHECK_VECTOR(host_vector_audio_meter_completion, 911);
    CHECK_VECTOR(host_vector_audio_meter_rejects_overlap, 259);
    return 0;
}
