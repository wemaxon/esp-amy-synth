#ifndef DIAGNOSTICS_SINE_OUTPUT_H
#define DIAGNOSTICS_SINE_OUTPUT_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_sine_output_run(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
