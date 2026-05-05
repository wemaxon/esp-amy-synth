#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_start_audio_test(const esp_pocket_audio_hw_t *hw);
void diagnostics_run(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
