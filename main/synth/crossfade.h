#pragma once

#include <stdint.h>

int32_t synth_crossfade_smooth_i32(int32_t from_sample,
                                   int32_t to_sample,
                                   uint32_t position,
                                   uint32_t length);
