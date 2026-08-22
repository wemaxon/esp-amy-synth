#ifndef HOST_STUB_SYNTH_AMY_RUNTIME_H
#define HOST_STUB_SYNTH_AMY_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

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

#endif

