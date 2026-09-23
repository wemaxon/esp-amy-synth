#include "synth/amy_runtime.h"
#include "esp_pocket_audio_board_config.h"

#include "amy.h"

#define RAW_PROFILE(_wave, _amp, _duty, _cutoff, _res, _attack, _decay, _sustain, _release) \
    { \
        .wave = (_wave), \
        .amp = (_amp), \
        .duty = (_duty), \
        .filter_cutoff_hz = (_cutoff), \
        .resonance = (_res), \
        .attack_ms = (_attack), \
        .decay_ms = (_decay), \
        .sustain_level = (_sustain), \
        .release_ms = (_release), \
    }

#define RAW_SOUND(_wave, _velocity, _amp, _duty, _cutoff, _res, _attack, _decay, _sustain, _release) \
    { \
        .backend = AMY_SOUND_BACKEND_RAW, \
        .velocity = (_velocity), \
        .raw = RAW_PROFILE((_wave), (_amp), (_duty), (_cutoff), (_res), (_attack), (_decay), (_sustain), (_release)), \
    }

#define PATCH_SOUND(_patch, _voices, _oscs, _velocity, _label) \
    { \
        .backend = AMY_SOUND_BACKEND_PATCH, \
        .patch_number = (_patch), \
        .patch_voices = (_voices), \
        .oscs_per_voice = (_oscs), \
        .velocity = (_velocity), \
        .patch_label = (_label), \
    }

#define PCM_SOUND(_preset, _loop, _velocity) \
    { \
        .backend = AMY_SOUND_BACKEND_PCM, \
        .pcm_preset = (_preset), \
        .pcm_loop = (_loop), \
        .patch_voices = ESP_POCKET_AUDIO_AMY_PCM_VOICES, \
        .oscs_per_voice = AMY_PCM_OSCS_PER_VOICE, \
        .velocity = (_velocity), \
        .raw = RAW_PROFILE(PCM, 0.70f, 0.5f, 18000.0f, 0.0f, 2, 260, 0.70f, 500), \
    }

static const amy_sound_runtime_t SOUND_RUNTIME[SYNTH_SOUND_COUNT] = {
    [SYNTH_SOUND_SAW] = RAW_SOUND(SAW_UP, 0.88f, 0.72f, 0.50f, 16000.0f, 0.20f, 5, 450, 0.74f, 850),
    [SYNTH_SOUND_SINE] = RAW_SOUND(SINE, 0.90f, 0.86f, 0.50f, 20000.0f, 0.00f, 6, 550, 0.78f, 900),
    [SYNTH_SOUND_TRIANGLE] = RAW_SOUND(TRIANGLE, 0.90f, 0.82f, 0.50f, 16000.0f, 0.08f, 8, 620, 0.72f, 900),
    [SYNTH_SOUND_SQUARE] = RAW_SOUND(PULSE, 0.88f, 0.76f, 0.50f, 14500.0f, 0.12f, 4, 460, 0.70f, 700),

    [SYNTH_SOUND_FM_EPIANO] = PATCH_SOUND(138, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.396f, "DX7 E.PIANO 1"),
    [SYNTH_SOUND_FM_BELL] = PATCH_SOUND(153, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.414f, "DX7 TUB BELLS"),
    [SYNTH_SOUND_FM_HX7] = PATCH_SOUND(141, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.396f, "DX7 SYN-LEAD 1"),

    [SYNTH_SOUND_STRINGS] = PATCH_SOUND(64, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.216f, "JUNO B11 STRINGS"),
    [SYNTH_SOUND_FLUTE] = PATCH_SOUND(3, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.558f, "JUNO A14 FLUTES"),
    [SYNTH_SOUND_BRASS] = PATCH_SOUND(0, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.459f, "JUNO A11 BRASS"),
    [SYNTH_SOUND_CLARINET] = PATCH_SOUND(105, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.522f, "JUNO B62 CLARINET"),

    [SYNTH_SOUND_WURLI] = PATCH_SOUND(138, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.342f, "DX7 E.PIANO 1"),
    [SYNTH_SOUND_VOX_AHH] = PATCH_SOUND(6, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.252f, "JUNO A17 CHOIR"),
    [SYNTH_SOUND_VIBRAPHONE] = PATCH_SOUND(148, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.558f, "DX7 VIBE 1"),
    [SYNTH_SOUND_ACOUSTIC] = PATCH_SOUND(139, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.036f, "DX7 GUITAR 1"),
    [SYNTH_SOUND_HARP] = PATCH_SOUND(188, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.45f, "DX7 HARP 1"),
    [SYNTH_SOUND_HUMMING] = PATCH_SOUND(6, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.153f, "JUNO A17 CHOIR"),
    [SYNTH_SOUND_ROBBO] = PATCH_SOUND(142, ESP_POCKET_AUDIO_AMY_DX7_VOICES, AMY_DX7_OSCS_PER_VOICE, 0.45f, "DX7 BASS 1"),
    [SYNTH_SOUND_SHUTTER] = PATCH_SOUND(61, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.522f, "JUNO A86 HAND CLAPS"),
    [SYNTH_SOUND_JUNO_POLY] = PATCH_SOUND(47, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.189f, "JUNO A68 SYNTH PAD"),
    [SYNTH_SOUND_OCEAN_PAD] = PATCH_SOUND(94, ESP_POCKET_AUDIO_AMY_JUNO_VOICES, AMY_JUNO_OSCS_PER_VOICE, 0.216f, "JUNO B47 PHASE ENSEMBLE"),

    [SYNTH_SOUND_SAWSQUARE] = RAW_SOUND(PULSE, 0.88f, 0.78f, 0.42f, 12000.0f, 0.18f, 5, 430, 0.68f, 720),
    [SYNTH_SOUND_MIC_SAMPLE] = PCM_SOUND(1024, false, 0.55f),
    [SYNTH_SOUND_USER_SAMPLE] = PCM_SOUND(1, false, 0.55f),
};

#undef PCM_SOUND
#undef PATCH_SOUND
#undef RAW_SOUND
#undef RAW_PROFILE

static uint8_t clamp_voice_count(uint8_t count)
{
    if (count == 0) {
        return 1;
    }
    if (count > AMY_MUSICAL_VOICE_COUNT) {
        return AMY_MUSICAL_VOICE_COUNT;
    }
    return count;
}

const amy_sound_runtime_t *amy_runtime_for_sound(synth_sound_t sound)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        sound = SYNTH_SOUND_SAW;
    }
    return &SOUND_RUNTIME[sound];
}

const char *amy_runtime_backend_name(amy_sound_backend_t backend)
{
    switch (backend) {
    case AMY_SOUND_BACKEND_RAW:
        return "raw";
    case AMY_SOUND_BACKEND_PATCH:
        return "patch";
    case AMY_SOUND_BACKEND_PCM:
        return "pcm";
    default:
        return "?";
    }
}

uint8_t amy_runtime_voice_limit(const amy_sound_runtime_t *runtime, uint8_t requested_voice_limit)
{
    uint8_t voice_limit = clamp_voice_count(requested_voice_limit);
    if (runtime == NULL || runtime->backend == AMY_SOUND_BACKEND_RAW ||
        runtime->backend == AMY_SOUND_BACKEND_PCM) {
        return voice_limit;
    }

    uint8_t runtime_limit = clamp_voice_count(runtime->patch_voices);
    return runtime_limit < voice_limit ? runtime_limit : voice_limit;
}

bool amy_runtime_supports_stereo_pairs(const amy_sound_runtime_t *runtime, bool stereo_enabled)
{
    return stereo_enabled && runtime != NULL;
}
