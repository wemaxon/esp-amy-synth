#ifndef DIAGNOSTICS_AMY_PATCH_BROWSER_H
#define DIAGNOSTICS_AMY_PATCH_BROWSER_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_amy_patch_browser_run(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
