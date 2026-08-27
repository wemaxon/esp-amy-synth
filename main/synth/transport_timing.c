#include "synth/transport_timing.h"

#include <limits.h>

uint64_t synth_transport_step_offset(uint32_t units_per_second,
                                     uint16_t bpm,
                                     uint32_t step_index,
                                     uint8_t steps_per_beat)
{
    if (units_per_second == 0 || bpm == 0 || steps_per_beat == 0) {
        return 0;
    }

    uint64_t numerator = (uint64_t)step_index * 60ULL * units_per_second;
    uint64_t denominator = (uint64_t)bpm * steps_per_beat;
    return numerator / denominator;
}

uint32_t synth_transport_bar_span(uint32_t units_per_second,
                                  uint16_t bpm,
                                  uint8_t bars)
{
    if (units_per_second == 0 || bpm == 0 || bars == 0) {
        return 0;
    }

    uint64_t span = ((uint64_t)bars * 4ULL * 60ULL * units_per_second) / bpm;
    return span > UINT32_MAX ? UINT32_MAX : (uint32_t)span;
}
