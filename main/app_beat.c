#include "app_beat.h"

#include <math.h>
#include <stddef.h>

#include "freertos/task.h"

typedef struct {
    bool initialized;
    float bpm;
    uint16_t ppqn;
    uint8_t beats_per_bar;
    TickType_t last_tick;
    double pulse_phase;
    uint64_t pulse_count;
} app_beat_state_t;

static app_beat_state_t s_state = {
    .initialized = false,
    .bpm = 120.0f,
    .ppqn = 24,
    .beats_per_bar = 4,
    .last_tick = 0,
    .pulse_phase = 0.0,
    .pulse_count = 0,
};

static float clamp_bpm(float bpm)
{
    if (bpm < 20.0f) {
        return 20.0f;
    }
    if (bpm > 300.0f) {
        return 300.0f;
    }
    return bpm;
}

bool app_beat_is_initialized(void)
{
    return s_state.initialized;
}

esp_err_t app_beat_init(float bpm, uint16_t ppqn, uint8_t beats_per_bar)
{
    if (ppqn == 0 || beats_per_bar == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state.initialized = true;
    s_state.bpm = clamp_bpm(bpm);
    s_state.ppqn = ppqn;
    s_state.beats_per_bar = beats_per_bar;
    s_state.last_tick = xTaskGetTickCount();
    s_state.pulse_phase = 0.0;
    s_state.pulse_count = 0;

    return ESP_OK;
}

esp_err_t app_beat_update(TickType_t now_ticks)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t dt_ticks = (int32_t)(now_ticks - s_state.last_tick);
    if (dt_ticks <= 0) {
        return ESP_OK;
    }

    double dt_seconds = (double)dt_ticks / (double)configTICK_RATE_HZ;
    double pulses_per_second = ((double)s_state.bpm * (double)s_state.ppqn) / 60.0;
    s_state.pulse_phase += dt_seconds * pulses_per_second;
    s_state.pulse_count = (uint64_t)floor(s_state.pulse_phase);
    s_state.last_tick = now_ticks;

    return ESP_OK;
}

esp_err_t app_beat_set_bpm(float bpm)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(app_beat_update(xTaskGetTickCount()));
    s_state.bpm = clamp_bpm(bpm);
    return ESP_OK;
}

float app_beat_get_bpm(void)
{
    return s_state.bpm;
}

uint16_t app_beat_get_ppqn(void)
{
    return s_state.ppqn;
}

uint8_t app_beat_get_beats_per_bar(void)
{
    return s_state.beats_per_bar;
}

uint64_t app_beat_now_pulse(void)
{
    return s_state.pulse_count;
}

uint64_t app_beat_pulses_per_bar(void)
{
    return (uint64_t)s_state.ppqn * (uint64_t)s_state.beats_per_bar;
}

double app_beat_ms_per_pulse(void)
{
    if (s_state.bpm <= 0.0f || s_state.ppqn == 0) {
        return 0.0;
    }
    return 60000.0 / ((double)s_state.bpm * (double)s_state.ppqn);
}
