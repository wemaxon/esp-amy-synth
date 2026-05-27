#include "synth/app_tempo.h"

static const TickType_t TAP_RESET_TICKS = pdMS_TO_TICKS(2000);

static uint16_t clamp_bpm(int bpm)
{
    if (bpm < SYNTH_APP_TEMPO_MIN_BPM) {
        return SYNTH_APP_TEMPO_MIN_BPM;
    }
    if (bpm > SYNTH_APP_TEMPO_MAX_BPM) {
        return SYNTH_APP_TEMPO_MAX_BPM;
    }
    return (uint16_t)bpm;
}

uint16_t synth_app_tempo_bpm(const synth_app_tempo_t *tempo)
{
    if (tempo == NULL || tempo->bpm == 0) {
        return SYNTH_APP_TEMPO_DEFAULT_BPM;
    }
    return tempo->bpm;
}

bool synth_app_tempo_step(synth_app_tempo_t *tempo, int16_t delta)
{
    if (tempo == NULL) {
        return false;
    }

    uint16_t next = clamp_bpm((int)synth_app_tempo_bpm(tempo) + delta);
    if (next == tempo->bpm) {
        return false;
    }
    tempo->bpm = next;
    return true;
}

synth_app_tempo_tap_result_t synth_app_tempo_tap(synth_app_tempo_t *tempo, TickType_t now)
{
    synth_app_tempo_tap_result_t result = {
        .bpm = synth_app_tempo_bpm(tempo),
    };
    if (tempo == NULL) {
        return result;
    }

    if (tempo->tap_count == 0 ||
        (int32_t)(now - tempo->tap_last_tick) > (int32_t)TAP_RESET_TICKS) {
        tempo->tap_count = 1;
        tempo->tap_first_tick = now;
        tempo->tap_last_tick = now;
        result.tap_count = tempo->tap_count;
        return result;
    }

    ++tempo->tap_count;
    tempo->tap_last_tick = now;
    result.tap_count = tempo->tap_count;
    if (tempo->tap_count < 3) {
        return result;
    }

    TickType_t span_ticks = tempo->tap_last_tick - tempo->tap_first_tick;
    uint32_t span_ms = (uint32_t)span_ticks * (uint32_t)portTICK_PERIOD_MS;
    if (span_ms == 0) {
        return result;
    }

    uint32_t beat_ms = span_ms / (uint32_t)(tempo->tap_count - 1U);
    if (beat_ms == 0) {
        return result;
    }

    tempo->bpm = clamp_bpm((int)(60000UL / beat_ms));
    result.bpm = tempo->bpm;
    result.changed = true;
    return result;
}
