#ifndef SYNTH_AMY_ENGINE_H
#define SYNTH_AMY_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"
#include "synth/chord_engine.h"
#include "synth/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t synth_amy_init(const esp_pocket_audio_hw_t *hw);
esp_err_t synth_amy_set_sound(synth_sound_t sound);
esp_err_t synth_amy_set_volume(float normalized_volume);
esp_err_t synth_amy_set_filter_enabled(bool enabled);
esp_err_t synth_amy_set_filter_cutoff(float cutoff_hz);
esp_err_t synth_amy_set_reverb_enabled(bool enabled);
esp_err_t synth_amy_set_chorus_enabled(bool enabled);
esp_err_t synth_amy_set_delay_enabled(bool enabled, float delay_ms);
esp_err_t synth_amy_set_tremolo_enabled(bool enabled);
esp_err_t synth_amy_set_tremolo_amount(float amount);
esp_err_t synth_amy_set_flanger_enabled(bool enabled);
esp_err_t synth_amy_set_lfo_enabled(bool enabled);
esp_err_t synth_amy_set_lfo_pitch(float semitones);
esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord);
esp_err_t synth_amy_chord_voice_on(uint8_t slot, const synth_chord_t *chord, uint8_t voice_index);
esp_err_t synth_amy_chord_off(uint8_t slot);
esp_err_t synth_amy_all_off(void);

#ifdef __cplusplus
}
#endif

#endif
