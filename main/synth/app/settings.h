#ifndef SYNTH_APP_SETTINGS_H
#define SYNTH_APP_SETTINGS_H

#include <stdint.h>

#include "synth/amy_engine.h"

typedef enum {
    SYNTH_APP_ADSR_PRESET_LONG = 0,
    SYNTH_APP_ADSR_PRESET_SHORT,
    SYNTH_APP_ADSR_PRESET_SWELL,
    SYNTH_APP_ADSR_PRESET_PLUCK,
    SYNTH_APP_ADSR_PRESET_TOUCH,
    SYNTH_APP_ADSR_PRESET_SUSTAIN,
    SYNTH_APP_ADSR_PRESET_COUNT,
} synth_app_adsr_preset_t;

#define SYNTH_APP_ADSR_DEFAULT_PRESET SYNTH_APP_ADSR_PRESET_TOUCH
#define SYNTH_APP_ADSR_DEFAULT_ATTACK_MS 25U
#define SYNTH_APP_ADSR_DEFAULT_RELEASE_MS 450U

#ifdef __cplusplus
extern "C" {
#endif

const char *synth_app_adsr_preset_name(uint8_t preset);
synth_amy_envelope_t synth_app_adsr_preset_envelope(uint8_t preset);
uint32_t synth_app_adsr_repeat_release_cap_ms(void);
uint32_t synth_app_attack_ms_from_wheel(uint8_t percent);
uint32_t synth_app_release_ms_from_wheel(uint8_t percent);
float synth_app_filter_cutoff_hz_from_wheel(uint8_t percent);

uint8_t synth_app_lfo_level_count(void);
float synth_app_lfo_depth_semitones(uint8_t level);

uint8_t synth_app_glide_level_count(void);
const char *synth_app_glide_level_name(uint8_t level);
uint16_t synth_app_glide_ms(uint8_t level);

uint8_t synth_app_delay_level_count(void);
const char *synth_app_delay_level_name(uint8_t level);
float synth_app_delay_ms_for_level(uint8_t level, uint16_t bpm);

uint8_t synth_app_tremolo_level_count(void);
const char *synth_app_tremolo_level_name(uint8_t level);
float synth_app_tremolo_rate_hz(uint8_t level, uint16_t bpm);
float synth_app_tremolo_depth(void);

uint8_t synth_app_voice_level_count(void);
const char *synth_app_voice_level_name(uint8_t level);
uint8_t synth_app_voice_limit(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif
