#ifndef SYNTH_AMY_ENGINE_H
#define SYNTH_AMY_ENGINE_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"
#include "synth/chord_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t synth_amy_init(const esp_pocket_audio_hw_t *hw);
esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord);
esp_err_t synth_amy_chord_off(uint8_t slot);
esp_err_t synth_amy_all_off(void);

#ifdef __cplusplus
}
#endif

#endif
