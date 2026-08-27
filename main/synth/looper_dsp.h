#ifndef SYNTH_LOOPER_DSP_H
#define SYNTH_LOOPER_DSP_H

#include <stdint.h>

#include "synth/looper.h"

#ifdef __cplusplus
extern "C" {
#endif

int16_t synth_looper_dsp_encode_sample(int32_t sample);
int32_t synth_looper_dsp_apply_gain_q8(int32_t sample, uint16_t gain_q8);
int32_t synth_looper_dsp_apply_percent_gain(int32_t sample, uint8_t percent);
uint16_t synth_looper_dsp_bus_gain_q8(uint8_t active_tracks);
void synth_looper_dsp_meter_peak(uint32_t *peak, int32_t sample);
uint8_t synth_looper_dsp_peak_percent(uint32_t peak);
int16_t synth_looper_dsp_saturate(int32_t sample, synth_looper_meter_t *meter);

#ifdef __cplusplus
}
#endif

#endif

