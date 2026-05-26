#ifndef SYNTH_AMY_PERF_H
#define SYNTH_AMY_PERF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void synth_amy_perf_reset(void);
void synth_amy_perf_record_block(uint32_t render_us,
                                 uint32_t amy_us,
                                 uint32_t flanger_us,
                                 uint32_t looper_us,
                                 uint32_t write_us,
                                 int64_t now_us,
                                 uint16_t app_active_oscs,
                                 uint16_t allocated_patch_oscs,
                                 uint32_t audio_block_budget_us);

#ifdef __cplusplus
}
#endif

#endif
