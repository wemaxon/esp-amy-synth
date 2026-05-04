#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_beat_init(float bpm, uint16_t ppqn, uint8_t beats_per_bar);

esp_err_t app_beat_update(TickType_t now_ticks);

esp_err_t app_beat_set_bpm(float bpm);

float app_beat_get_bpm(void);

uint16_t app_beat_get_ppqn(void);

uint8_t app_beat_get_beats_per_bar(void);

uint64_t app_beat_now_pulse(void);

uint64_t app_beat_pulses_per_bar(void);

double app_beat_ms_per_pulse(void);

bool app_beat_is_initialized(void);

#ifdef __cplusplus
}
#endif
