#ifndef SYNTH_PITCH_DETECTOR_H
#define SYNTH_PITCH_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool pitched;
    float frequency_hz;
    float midi_note;
    float confidence;
} synth_pitch_result_t;

void synth_pitch_detect(const int16_t *samples,
                        uint32_t frame_count,
                        uint32_t sample_rate_hz,
                        synth_pitch_result_t *result);

#endif
