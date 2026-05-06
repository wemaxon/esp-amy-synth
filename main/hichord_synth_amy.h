#ifndef HICHORD_SYNTH_AMY_H
#define HICHORD_SYNTH_AMY_H

#include "esp_err.h"
#include "esp_pocket_audio.h"
#include "hichord_theory.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hichord_synth_amy_init(const esp_pocket_audio_hw_t *hw);
esp_err_t hichord_synth_amy_play_chord(const hichord_chord_t *chord);
esp_err_t hichord_synth_amy_release_chord(void);

#ifdef __cplusplus
}
#endif

#endif
