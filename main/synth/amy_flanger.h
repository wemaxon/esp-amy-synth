#ifndef SYNTH_AMY_FLANGER_H
#define SYNTH_AMY_FLANGER_H

#include <stdbool.h>
#include <stdint.h>

#include "amy.h"

#ifdef __cplusplus
extern "C" {
#endif

void synth_amy_flanger_set_enabled(bool enabled);
/* Processes one AMY bus block in place: AMY_NCHANS sequential channel blocks
 * of len samples, before the final mix. Runs in AMY's render. */
void synth_amy_flanger_process(SAMPLE *buf, uint16_t len);
/* Duration of the most recent process call, for the audio perf log. */
uint32_t synth_amy_flanger_last_process_us(void);

#ifdef __cplusplus
}
#endif

#endif
