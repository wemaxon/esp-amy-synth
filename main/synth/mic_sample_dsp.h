#ifndef SYNTH_MIC_SAMPLE_DSP_H
#define SYNTH_MIC_SAMPLE_DSP_H

#include <stdint.h>

typedef struct {
    float total_gain;
    float analog_gain_db;
    float digital_gain;
} synth_mic_gain_plan_t;

typedef struct {
    uint16_t peak;
    uint32_t clipped_samples;
} synth_mic_capture_meter_t;

float synth_mic_gain_from_percent(uint8_t percent);
void synth_mic_gain_plan(uint8_t percent, float applied_analog_db, synth_mic_gain_plan_t *plan);
uint32_t synth_mic_copy_input_channel(int16_t *destination,
                                      uint32_t capacity_frames,
                                      uint32_t write_frame,
                                      const int16_t *interleaved,
                                      uint32_t input_frames,
                                      uint8_t channels,
                                      uint8_t input_channel,
                                      int32_t digital_gain_q16,
                                      synth_mic_capture_meter_t *meter);

#endif
