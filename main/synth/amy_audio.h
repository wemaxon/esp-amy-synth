#ifndef SYNTH_AMY_AUDIO_H
#define SYNTH_AMY_AUDIO_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int16_t *(*synth_amy_audio_render_cb_t)(void);
typedef uint16_t (*synth_amy_audio_count_cb_t)(void);

typedef struct {
    const esp_pocket_audio_hw_t *hw;
    synth_amy_audio_render_cb_t render;
    synth_amy_audio_count_cb_t active_osc_count;
    synth_amy_audio_count_cb_t allocated_patch_osc_count;
} synth_amy_audio_config_t;

uint32_t synth_amy_audio_block_us(void);
esp_err_t synth_amy_audio_start(const synth_amy_audio_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
