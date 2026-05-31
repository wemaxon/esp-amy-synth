#include "synth/app/settings.h"

#include <math.h>
#include <stddef.h>

typedef struct {
    const char *name;
    synth_amy_envelope_t envelope;
} adsr_preset_def_t;

static const adsr_preset_def_t ADSR_PRESETS[SYNTH_APP_ADSR_PRESET_COUNT] = {
    [SYNTH_APP_ADSR_PRESET_LONG] = {"LONG", {800, 1000, 0.70f, 2000}},
    [SYNTH_APP_ADSR_PRESET_SHORT] = {"SHORT", {100, 100, 1.00f, 120}},
    [SYNTH_APP_ADSR_PRESET_SWELL] = {"SWELL", {800, 300, 0.80f, 2000}},
    [SYNTH_APP_ADSR_PRESET_PLUCK] = {"PLUCK", {5, 80, 0.00f, 180}},
    [SYNTH_APP_ADSR_PRESET_TOUCH] = {"TOUCH", {25, 260, 0.66f, 450}},
    [SYNTH_APP_ADSR_PRESET_SUSTAIN] = {"SUSTAIN", {200, 300, 0.85f, 3000}},
};

static const float LFO_DEPTH_SEMITONES[] = {
    0.0f, 0.12f, 0.35f, 0.75f,
};

static const uint16_t GLIDE_LEVEL_MS[] = {
    0, 60, 140, 300,
};

static const char *GLIDE_LEVEL_NAMES[] = {
    "OFF", "FAST", "MED", "SLOW",
};

static const char *DELAY_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T",
};

static const float DELAY_LEVEL_DIVISORS[] = {
    0.0f, 1.0f, 2.0f, 4.0f, 6.0f,
};

static const char *TREMOLO_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T", "1/32",
};

static const float TREMOLO_LEVEL_DIVISORS[] = {
    0.0f, 1.0f, 2.0f, 4.0f, 6.0f, 8.0f,
};

static const uint8_t VOICE_LIMITS[] = {
    6, 4, 2, 1,
};

static const char *VOICE_LEVEL_NAMES[] = {
    "8", "4", "2", "1",
};

static const uint32_t ATTACK_MIN_MS = 1;
static const uint32_t ATTACK_MAX_MS = 2000;
static const uint32_t RELEASE_MIN_MS = 1;
static const uint32_t RELEASE_MAX_MS = 5000;
static const uint32_t REPEAT_RELEASE_CAP_MS = 200;
static const float TREMOLO_DEPTH = 0.78f;

static uint8_t count_of_size(size_t count)
{
    return count > UINT8_MAX ? UINT8_MAX : (uint8_t)count;
}

static uint8_t normalize_index(uint8_t index, size_t count)
{
    if (count == 0 || index >= count) {
        return 0;
    }
    return index;
}

static uint8_t clamp_percent(uint8_t percent)
{
    return percent > 100U ? 100U : percent;
}

static uint32_t wheel_to_range_ms(uint8_t percent, uint32_t min_ms, uint32_t max_ms)
{
    percent = clamp_percent(percent);
    uint32_t span = max_ms - min_ms;
    return min_ms + ((span * (uint32_t)percent + 50U) / 100U);
}

const char *synth_app_adsr_preset_name(uint8_t preset)
{
    preset = normalize_index(preset, SYNTH_APP_ADSR_PRESET_COUNT);
    return ADSR_PRESETS[preset].name;
}

synth_amy_envelope_t synth_app_adsr_preset_envelope(uint8_t preset)
{
    if (preset >= SYNTH_APP_ADSR_PRESET_COUNT) {
        preset = SYNTH_APP_ADSR_DEFAULT_PRESET;
    }
    return ADSR_PRESETS[preset].envelope;
}

uint32_t synth_app_adsr_repeat_release_cap_ms(void)
{
    return REPEAT_RELEASE_CAP_MS;
}

uint32_t synth_app_attack_ms_from_wheel(uint8_t percent)
{
    return wheel_to_range_ms(percent, ATTACK_MIN_MS, ATTACK_MAX_MS);
}

uint32_t synth_app_release_ms_from_wheel(uint8_t percent)
{
    return wheel_to_range_ms(percent, RELEASE_MIN_MS, RELEASE_MAX_MS);
}

float synth_app_filter_cutoff_hz_from_wheel(uint8_t percent)
{
    float t = (float)clamp_percent(percent) / 100.0f;
    return 20.0f * powf(1000.0f, t);
}

uint8_t synth_app_lfo_level_count(void)
{
    return count_of_size(sizeof(LFO_DEPTH_SEMITONES) / sizeof(LFO_DEPTH_SEMITONES[0]));
}

float synth_app_lfo_depth_semitones(uint8_t level)
{
    level = normalize_index(level, sizeof(LFO_DEPTH_SEMITONES) / sizeof(LFO_DEPTH_SEMITONES[0]));
    return LFO_DEPTH_SEMITONES[level];
}

uint8_t synth_app_glide_level_count(void)
{
    return count_of_size(sizeof(GLIDE_LEVEL_MS) / sizeof(GLIDE_LEVEL_MS[0]));
}

const char *synth_app_glide_level_name(uint8_t level)
{
    level = normalize_index(level, sizeof(GLIDE_LEVEL_NAMES) / sizeof(GLIDE_LEVEL_NAMES[0]));
    return GLIDE_LEVEL_NAMES[level];
}

uint16_t synth_app_glide_ms(uint8_t level)
{
    level = normalize_index(level, sizeof(GLIDE_LEVEL_MS) / sizeof(GLIDE_LEVEL_MS[0]));
    return GLIDE_LEVEL_MS[level];
}

uint8_t synth_app_delay_level_count(void)
{
    return count_of_size(sizeof(DELAY_LEVEL_DIVISORS) / sizeof(DELAY_LEVEL_DIVISORS[0]));
}

const char *synth_app_delay_level_name(uint8_t level)
{
    level = normalize_index(level, sizeof(DELAY_LEVEL_NAMES) / sizeof(DELAY_LEVEL_NAMES[0]));
    return DELAY_LEVEL_NAMES[level];
}

float synth_app_delay_ms_for_level(uint8_t level, uint16_t bpm)
{
    level = normalize_index(level, sizeof(DELAY_LEVEL_DIVISORS) / sizeof(DELAY_LEVEL_DIVISORS[0]));
    if (level == 0 || bpm == 0) {
        return 0.0f;
    }

    return (60000.0f / (float)bpm) / DELAY_LEVEL_DIVISORS[level];
}

uint8_t synth_app_tremolo_level_count(void)
{
    return count_of_size(sizeof(TREMOLO_LEVEL_DIVISORS) / sizeof(TREMOLO_LEVEL_DIVISORS[0]));
}

const char *synth_app_tremolo_level_name(uint8_t level)
{
    level = normalize_index(level, sizeof(TREMOLO_LEVEL_NAMES) / sizeof(TREMOLO_LEVEL_NAMES[0]));
    return TREMOLO_LEVEL_NAMES[level];
}

float synth_app_tremolo_rate_hz(uint8_t level, uint16_t bpm)
{
    level = normalize_index(level, sizeof(TREMOLO_LEVEL_DIVISORS) / sizeof(TREMOLO_LEVEL_DIVISORS[0]));
    if (level == 0) {
        return 0.0f;
    }

    return ((float)bpm / 60.0f) * TREMOLO_LEVEL_DIVISORS[level];
}

float synth_app_tremolo_depth(void)
{
    return TREMOLO_DEPTH;
}

uint8_t synth_app_voice_level_count(void)
{
    return count_of_size(sizeof(VOICE_LIMITS) / sizeof(VOICE_LIMITS[0]));
}

const char *synth_app_voice_level_name(uint8_t level)
{
    level = normalize_index(level, sizeof(VOICE_LEVEL_NAMES) / sizeof(VOICE_LEVEL_NAMES[0]));
    return VOICE_LEVEL_NAMES[level];
}

uint8_t synth_app_voice_limit(uint8_t level)
{
    level = normalize_index(level, sizeof(VOICE_LIMITS) / sizeof(VOICE_LIMITS[0]));
    return VOICE_LIMITS[level];
}
