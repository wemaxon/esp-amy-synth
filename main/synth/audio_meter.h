#ifndef SYNTH_AUDIO_METER_H
#define SYNTH_AUDIO_METER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_AUDIO_METER_TAP_DRY = 0,
    SYNTH_AUDIO_METER_TAP_OUTPUT,
    SYNTH_AUDIO_METER_TAP_COUNT,
} synth_audio_meter_tap_t;

typedef void (*synth_audio_meter_boundary_hook_t)(void *context);

typedef struct {
    uint32_t session_id;
    synth_audio_meter_tap_t tap;
    uint32_t target_frames;
    uint32_t stop_after_frames;
    synth_audio_meter_boundary_hook_t on_start;
    synth_audio_meter_boundary_hook_t on_stop;
    void *hook_context;
} synth_audio_meter_request_t;

/*
 * Linear measurements are published so the audio task never evaluates
 * logarithms.  Diagnostic/reporting code converts them to dBFS and LUFS.
 */
typedef struct {
    uint32_t session_id;
    synth_audio_meter_tap_t tap;
    uint32_t sample_rate_hz;
    uint64_t measured_frames;
    uint32_t target_frames;
    uint64_t clipped_samples;
    float sample_peak;
    float mean_square;
    float k_weighted_mean_square;
    float max_momentary_k_mean_square;
    float k_weighted_exposure;
    uint32_t process_time_max_us;
    uint32_t process_deadline_misses;
    bool active;
    bool complete;
} synth_audio_meter_result_t;

esp_err_t synth_audio_meter_init(uint32_t sample_rate_hz,
                                 uint32_t audio_block_frames,
                                 uint32_t audio_block_budget_us);
esp_err_t synth_audio_meter_start(const synth_audio_meter_request_t *request);
bool synth_audio_meter_read(synth_audio_meter_result_t *result);

/*
 * Audio-task-only entry points. begin_block activates requests and invokes
 * optional fixture hooks exactly at a render-block boundary. Both calls are
 * bounded, allocation-free, and lock-free.
 */
void synth_audio_meter_begin_block(void);
void synth_audio_meter_process(synth_audio_meter_tap_t tap,
                               const int16_t *interleaved_samples,
                               uint32_t frames,
                               uint8_t channels);

#ifdef __cplusplus
}
#endif

#endif
