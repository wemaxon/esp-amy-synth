#ifndef SYNTH_TRANSPORT_TIMING_H
#define SYNTH_TRANSPORT_TIMING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t synth_transport_step_offset(uint32_t units_per_second,
                                     uint16_t bpm,
                                     uint32_t step_index,
                                     uint8_t steps_per_beat);
uint32_t synth_transport_bar_span(uint32_t units_per_second,
                                  uint16_t bpm,
                                  uint8_t bars);

#ifdef __cplusplus
}
#endif

#endif
