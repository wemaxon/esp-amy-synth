#ifndef SYNTH_MIC_SAMPLE_H
#define SYNTH_MIC_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "synth/mic_sample_dsp.h"
#include "synth/pitch_detector.h"

typedef enum {
    SYNTH_MIC_SAMPLE_EMPTY = 0,
    SYNTH_MIC_SAMPLE_RECORDING,
    SYNTH_MIC_SAMPLE_CAPTURED,
    SYNTH_MIC_SAMPLE_ANALYZING,
    SYNTH_MIC_SAMPLE_READY,
    SYNTH_MIC_SAMPLE_ERROR,
} synth_mic_sample_state_t;

typedef struct {
    synth_mic_sample_state_t state;
    uint32_t frames;
    uint32_t capacity_frames;
    uint16_t peak;
    uint32_t clipped_samples;
    synth_pitch_result_t pitch;
} synth_mic_sample_snapshot_t;

esp_err_t synth_mic_sample_init(uint32_t sample_rate_hz, uint32_t max_seconds);
esp_err_t synth_mic_sample_begin(float digital_gain);
void synth_mic_sample_request_stop(void);
void synth_mic_sample_cancel(void);
bool synth_mic_sample_is_recording(void);
void synth_mic_sample_process_input(const int16_t *interleaved,
                                    uint32_t frames,
                                    uint8_t channels,
                                    uint8_t input_channel);
esp_err_t synth_mic_sample_analyze(void);
void synth_mic_sample_mark_installed(bool installed);
void synth_mic_sample_snapshot(synth_mic_sample_snapshot_t *snapshot);
const int16_t *synth_mic_sample_data(uint32_t *frames);

#endif
