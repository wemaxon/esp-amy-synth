#ifndef SYNTH_AMY_FLANGER_H
#define SYNTH_AMY_FLANGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void synth_amy_flanger_set_enabled(bool enabled);
void synth_amy_flanger_process(int16_t *block);

#ifdef __cplusplus
}
#endif

#endif
