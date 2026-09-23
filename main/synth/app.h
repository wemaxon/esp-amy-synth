#ifndef SYNTH_APP_H
#define SYNTH_APP_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t synth_app_start(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
