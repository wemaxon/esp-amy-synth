#include "synth/crossfade.h"

static const uint32_t CROSSFADE_Q15_ONE = 32768U;

int32_t synth_crossfade_smooth_i32(int32_t from_sample,
                                   int32_t to_sample,
                                   uint32_t position,
                                   uint32_t length)
{
    if (length <= 1U || position >= length - 1U) {
        return to_sample;
    }

    uint32_t t = (uint32_t)(((uint64_t)(position + 1U) * CROSSFADE_Q15_ONE) /
                            (uint64_t)length);
    if (t > CROSSFADE_Q15_ONE) {
        t = CROSSFADE_Q15_ONE;
    }

    uint32_t t2 = (uint32_t)(((uint64_t)t * (uint64_t)t) >> 15);
    uint32_t smooth = (uint32_t)(((uint64_t)t2 *
                                  (uint64_t)((3U * CROSSFADE_Q15_ONE) - (2U * t))) >>
                                 15);
    if (smooth > CROSSFADE_Q15_ONE) {
        smooth = CROSSFADE_Q15_ONE;
    }

    int64_t from_weight = (int64_t)(CROSSFADE_Q15_ONE - smooth);
    int64_t to_weight = (int64_t)smooth;
    return (int32_t)(((int64_t)from_sample * from_weight +
                      (int64_t)to_sample * to_weight) /
                     (int64_t)CROSSFADE_Q15_ONE);
}
