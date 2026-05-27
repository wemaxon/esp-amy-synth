#include "synth/app_modulation.h"

#include <math.h>
#include <stddef.h>

#include "synth/app_settings.h"

static const float LFO_RATE_HZ = 5.0f;
static const float TWO_PI = 6.283185307f;

static bool advance_phase(synth_app_modulation_t *modulation,
                          float rate_hz,
                          TickType_t now)
{
    if (modulation == NULL || rate_hz <= 0.0f) {
        return false;
    }
    if (modulation->last_tick == 0) {
        modulation->last_tick = now;
        return false;
    }

    TickType_t elapsed_ticks = now - modulation->last_tick;
    modulation->last_tick = now;
    if (elapsed_ticks == 0) {
        return false;
    }

    float elapsed_seconds = ((float)elapsed_ticks * (float)portTICK_PERIOD_MS) / 1000.0f;
    modulation->phase += TWO_PI * rate_hz * elapsed_seconds;
    while (modulation->phase >= TWO_PI) {
        modulation->phase -= TWO_PI;
    }
    return true;
}

void synth_app_modulation_reset(synth_app_modulation_t *modulation, TickType_t now)
{
    if (modulation == NULL) {
        return;
    }
    modulation->phase = 0.0f;
    modulation->last_tick = now;
}

bool synth_app_lfo_pitch(synth_app_modulation_t *modulation,
                         uint8_t level,
                         TickType_t now,
                         float *pitch_semitones)
{
    if (pitch_semitones == NULL ||
        level == 0 ||
        level >= synth_app_lfo_level_count() ||
        !advance_phase(modulation, LFO_RATE_HZ, now)) {
        return false;
    }

    *pitch_semitones = sinf(modulation->phase) * synth_app_lfo_depth_semitones(level);
    return true;
}

bool synth_app_tremolo_amount(synth_app_modulation_t *modulation,
                              uint8_t level,
                              uint16_t bpm,
                              TickType_t now,
                              float *amount)
{
    if (amount == NULL ||
        level == 0 ||
        level >= synth_app_tremolo_level_count() ||
        !advance_phase(modulation, synth_app_tremolo_rate_hz(level, bpm), now)) {
        return false;
    }

    *amount = synth_app_tremolo_depth() * (0.5f + 0.5f * sinf(modulation->phase));
    return true;
}
