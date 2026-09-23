#ifndef SYNTH_AMY_RUNTIME_H
#define SYNTH_AMY_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/sound.h"

#define AMY_MUSICAL_VOICE_COUNT 6
#define AMY_STEREO_PAIR_OFFSET AMY_MUSICAL_VOICE_COUNT
#define AMY_PATCH_MAIN_SYNTH 0
#define AMY_PATCH_PAIR_SYNTH 1
#define AMY_PATCH_SYNTH_COUNT (ESP_POCKET_AUDIO_AMY_STEREO_PAIRS ? 2U : 1U)
#define AMY_JUNO_OSCS_PER_VOICE 5
#define AMY_DX7_OSCS_PER_VOICE 8
#define AMY_PCM_OSCS_PER_VOICE 1

typedef enum {
    AMY_SOUND_BACKEND_RAW = 0,
    AMY_SOUND_BACKEND_PATCH,
    AMY_SOUND_BACKEND_PCM,
} amy_sound_backend_t;

typedef struct {
    uint8_t wave;
    float amp;
    float duty;
    float filter_cutoff_hz;
    float resonance;
    uint32_t attack_ms;
    uint32_t decay_ms;
    float sustain_level;
    uint32_t release_ms;
} amy_raw_profile_t;

typedef struct {
    amy_sound_backend_t backend;
    uint16_t patch_number;
    uint8_t patch_voices;
    uint8_t oscs_per_voice;
    int16_t pcm_preset;
    bool pcm_loop;
    float velocity;
    const char *patch_label;
    amy_raw_profile_t raw;
} amy_sound_runtime_t;

#ifdef __cplusplus
extern "C" {
#endif

const amy_sound_runtime_t *amy_runtime_for_sound(synth_sound_t sound);
const char *amy_runtime_backend_name(amy_sound_backend_t backend);
uint8_t amy_runtime_voice_limit(const amy_sound_runtime_t *runtime, uint8_t requested_voice_limit);
bool amy_runtime_supports_stereo_pairs(const amy_sound_runtime_t *runtime, bool stereo_enabled);

#ifdef __cplusplus
}
#endif

#endif
