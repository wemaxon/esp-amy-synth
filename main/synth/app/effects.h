#ifndef SYNTH_APP_EFFECTS_H
#define SYNTH_APP_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_pocket_audio_board_config.h"
#include "hmi/play_ui.h"
#include "synth/amy_engine.h"
#include "synth/app/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_EFFECTS_INITIALIZER \
    { \
        .effect_index = PLAY_UI_EFFECT_JOYSTICK, \
        .enabled = { \
            [PLAY_UI_EFFECT_FILTER] = true, \
            [PLAY_UI_EFFECT_STEREO] = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0, \
        }, \
        .adsr_preset = SYNTH_APP_ADSR_DEFAULT_PRESET, \
        .attack_ms = SYNTH_APP_ADSR_DEFAULT_ATTACK_MS, \
        .release_ms = SYNTH_APP_ADSR_DEFAULT_RELEASE_MS, \
    }

typedef struct {
    uint8_t effect_index;
    bool enabled[PLAY_UI_EFFECT_COUNT];
    uint8_t lfo_level;
    uint8_t delay_level;
    uint8_t tremolo_level;
    uint8_t voice_level;
    uint8_t adsr_preset;
    bool adsr_modified;
    uint32_t attack_ms;
    uint32_t release_ms;
    uint8_t glide_level;
} synth_app_effects_t;

play_ui_effect_t synth_app_effects_selected(const synth_app_effects_t *effects);
void synth_app_effects_step_selected(synth_app_effects_t *effects, int8_t steps);
void synth_app_effects_toggle_enabled(synth_app_effects_t *effects, play_ui_effect_t effect);
void synth_app_effects_step_lfo_level(synth_app_effects_t *effects);
void synth_app_effects_step_delay_level(synth_app_effects_t *effects);
void synth_app_effects_step_tremolo_level(synth_app_effects_t *effects);
void synth_app_effects_step_voice_level(synth_app_effects_t *effects);
void synth_app_effects_step_adsr_preset(synth_app_effects_t *effects);
void synth_app_effects_step_glide_level(synth_app_effects_t *effects);
void synth_app_effects_set_attack_ms(synth_app_effects_t *effects, uint32_t attack_ms);
void synth_app_effects_set_release_ms(synth_app_effects_t *effects, uint32_t release_ms);
synth_amy_envelope_t synth_app_effects_envelope(const synth_app_effects_t *effects,
                                                bool repeat_mode);
uint16_t synth_app_effects_glide_ms(const synth_app_effects_t *effects);

#ifdef __cplusplus
}
#endif

#endif
