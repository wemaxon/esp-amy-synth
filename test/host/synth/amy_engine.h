#ifndef HOST_STUB_SYNTH_AMY_ENGINE_H
#define HOST_STUB_SYNTH_AMY_ENGINE_H

#include <stdint.h>

typedef struct {
    uint32_t attack_ms;
    uint32_t decay_ms;
    float sustain_level;
    uint32_t release_ms;
} synth_amy_envelope_t;

#endif

