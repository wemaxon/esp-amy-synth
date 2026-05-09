#ifndef DIAGNOSTICS_DIGITAL_SILENCE_H
#define DIAGNOSTICS_DIGITAL_SILENCE_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_digital_silence_start(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
