#ifndef SYNTH_AMY_ENGINE_H
#define SYNTH_AMY_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
typedef struct esp_pocket_audio_hw esp_pocket_audio_hw_t;
#include "synth/chord_engine.h"
#include "synth/sound.h"

#define SYNTH_AMY_BUILTIN_PATCH_COUNT 258U

typedef struct {
    uint32_t attack_ms;
    uint32_t decay_ms;
    float sustain_level;
    uint32_t release_ms;
} synth_amy_envelope_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t synth_amy_init(const esp_pocket_audio_hw_t *hw);
/* Exclusive-mode reset: discard pending events and restore a fresh AMY graph. */
esp_err_t synth_amy_reset_exclusive(void);
esp_err_t synth_amy_set_sound(synth_sound_t sound);
esp_err_t synth_amy_install_mic_sample(const int16_t *samples,
                                       uint32_t frames,
                                       float reference_midi_note);
bool synth_amy_mic_sample_ready(void);
float synth_amy_sound_velocity(synth_sound_t sound);
bool synth_amy_sound_patch_info(synth_sound_t sound, uint16_t *patch_number, const char **patch_label);
esp_err_t synth_amy_set_builtin_patch(uint16_t patch_number,
                                      uint8_t voices,
                                      uint8_t oscs_per_voice);
esp_err_t synth_amy_builtin_patch_note_on(uint8_t midi_note, float velocity);
esp_err_t synth_amy_builtin_patch_note_off(uint8_t midi_note);
esp_err_t synth_amy_drum_hit(uint8_t pcm_preset, uint8_t midi_note, uint8_t velocity);
esp_err_t synth_amy_drum_hit_with_gain(uint8_t pcm_preset,
                                      uint8_t midi_note,
                                      uint8_t velocity,
                                      float level_gain);
esp_err_t synth_amy_set_volume(float normalized_volume);
esp_err_t synth_amy_set_filter_enabled(bool enabled);
esp_err_t synth_amy_set_filter_cutoff(float cutoff_hz);
esp_err_t synth_amy_set_reverb_enabled(bool enabled);
esp_err_t synth_amy_set_chorus_enabled(bool enabled);
esp_err_t synth_amy_set_delay_enabled(bool enabled, float delay_ms);
esp_err_t synth_amy_set_tremolo_enabled(bool enabled);
esp_err_t synth_amy_set_tremolo_amount(float amount);
esp_err_t synth_amy_set_flanger_enabled(bool enabled);
esp_err_t synth_amy_set_stereo_enabled(bool enabled);
esp_err_t synth_amy_set_lfo_enabled(bool enabled);
esp_err_t synth_amy_set_lfo_pitch(float semitones);
esp_err_t synth_amy_set_voice_limit(uint8_t voices);
esp_err_t synth_amy_set_envelope(const synth_amy_envelope_t *envelope);
esp_err_t synth_amy_set_portamento(uint16_t portamento_ms);
esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord);
esp_err_t synth_amy_chord_voice_on(uint8_t slot, const synth_chord_t *chord, uint8_t voice_index);
esp_err_t synth_amy_chord_off(uint8_t slot);
esp_err_t synth_amy_musical_all_off(void);
esp_err_t synth_amy_all_off(void);

#ifdef __cplusplus
}
#endif

#endif
