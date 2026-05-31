#include "synth/app/effects.h"

static uint8_t next_level(uint8_t current, uint8_t count)
{
    if (count == 0) {
        return 0;
    }
    return (uint8_t)((current + 1U) % count);
}

play_ui_effect_t synth_app_effects_selected(const synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return PLAY_UI_EFFECT_REVERB;
    }
    return (play_ui_effect_t)(effects->effect_index % PLAY_UI_EFFECT_COUNT);
}

void synth_app_effects_step_selected(synth_app_effects_t *effects, int8_t steps)
{
    if (effects == NULL) {
        return;
    }

    int value = (int)effects->effect_index + steps;
    while (value < 0) {
        value += PLAY_UI_EFFECT_COUNT;
    }
    effects->effect_index = (uint8_t)(value % PLAY_UI_EFFECT_COUNT);
}

void synth_app_effects_toggle_enabled(synth_app_effects_t *effects, play_ui_effect_t effect)
{
    if (effects == NULL || effect >= PLAY_UI_EFFECT_COUNT) {
        return;
    }
    effects->enabled[effect] = !effects->enabled[effect];
}

void synth_app_effects_step_lfo_level(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->lfo_level = next_level(effects->lfo_level, synth_app_lfo_level_count());
    effects->enabled[PLAY_UI_EFFECT_LFO] = effects->lfo_level > 0;
}

void synth_app_effects_step_delay_level(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->delay_level = next_level(effects->delay_level, synth_app_delay_level_count());
    effects->enabled[PLAY_UI_EFFECT_DELAY] = effects->delay_level > 0;
}

void synth_app_effects_step_tremolo_level(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->tremolo_level = next_level(effects->tremolo_level, synth_app_tremolo_level_count());
    effects->enabled[PLAY_UI_EFFECT_TREMOLO] = effects->tremolo_level > 0;
}

void synth_app_effects_step_voice_level(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->voice_level = next_level(effects->voice_level, synth_app_voice_level_count());
}

void synth_app_effects_step_adsr_preset(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->adsr_preset = next_level(effects->adsr_preset, SYNTH_APP_ADSR_PRESET_COUNT);
    synth_amy_envelope_t preset = synth_app_adsr_preset_envelope(effects->adsr_preset);
    effects->attack_ms = preset.attack_ms;
    effects->release_ms = preset.release_ms;
    effects->adsr_modified = true;
}

void synth_app_effects_step_glide_level(synth_app_effects_t *effects)
{
    if (effects == NULL) {
        return;
    }
    effects->glide_level = next_level(effects->glide_level, synth_app_glide_level_count());
}

void synth_app_effects_set_attack_ms(synth_app_effects_t *effects, uint32_t attack_ms)
{
    if (effects == NULL) {
        return;
    }
    effects->attack_ms = attack_ms;
    effects->adsr_modified = true;
}

void synth_app_effects_set_release_ms(synth_app_effects_t *effects, uint32_t release_ms)
{
    if (effects == NULL) {
        return;
    }
    effects->release_ms = release_ms;
    effects->adsr_modified = true;
}

synth_amy_envelope_t synth_app_effects_envelope(const synth_app_effects_t *effects,
                                                bool repeat_mode)
{
    uint8_t preset = effects != NULL ? effects->adsr_preset : SYNTH_APP_ADSR_DEFAULT_PRESET;
    synth_amy_envelope_t envelope = synth_app_adsr_preset_envelope(preset);
    if (effects != NULL) {
        envelope.attack_ms = effects->attack_ms;
        envelope.release_ms = effects->release_ms;
    }

    uint32_t repeat_cap_ms = synth_app_adsr_repeat_release_cap_ms();
    if (repeat_mode && envelope.release_ms > repeat_cap_ms) {
        envelope.release_ms = repeat_cap_ms;
    }
    return envelope;
}

uint16_t synth_app_effects_glide_ms(const synth_app_effects_t *effects)
{
    return synth_app_glide_ms(effects != NULL ? effects->glide_level : 0);
}
