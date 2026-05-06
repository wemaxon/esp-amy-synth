#ifndef HICHORD_APP_H
#define HICHORD_APP_H

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hichord_app_start(const esp_pocket_audio_hw_t *hw);

#ifdef __cplusplus
}
#endif

#endif
