#ifndef DIAGNOSTICS_DIAGNOSTIC_UI_H
#define DIAGNOSTICS_DIAGNOSTIC_UI_H

#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

void diagnostic_ui_run(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
