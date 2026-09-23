#ifndef DIAGNOSTICS_LEVEL_CALIBRATION_H
#define DIAGNOSTICS_LEVEL_CALIBRATION_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exclusive, deterministic on-device fixture sweep. */
esp_err_t diagnostics_level_calibration_run(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
