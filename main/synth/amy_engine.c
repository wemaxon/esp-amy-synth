#include "synth/amy_engine.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "synth/amy_flanger.h"
#include "synth/amy_perf.h"
#include "synth/amy_runtime.h"
#include "synth/amy_voice_map.h"
#include "synth/looper.h"


#define AMY_STEREO_MIN_OSCS ((AMY_MUSICAL_VOICE_COUNT * AMY_PIANO_OSCS_PER_VOICE * 2U) + 20U)
#define SYNTH_DELAY_MAX_SAMPLES 32768U
#define SYNTH_DELAY_MAX_MS      (((float)SYNTH_DELAY_MAX_SAMPLES * 1000.0f) / (float)AMY_SAMPLE_RATE)
#define SYNTH_AUDIO_BLOCK_US    (((uint32_t)AMY_BLOCK_SIZE * 1000000U) / (uint32_t)AMY_SAMPLE_RATE)

typedef struct {
    bool active;
    bool pair_active;
    amy_sound_backend_t backend;
    uint8_t slot;
    uint8_t note;
    uint32_t age;
} amy_voice_t;

static const char *TAG = "SYNTH_AMY";

static const float RAW_NOTE_AMP = 0.62f;
static const float RAW_STEREO_PAIR_AMP_SCALE = 0.62f;
static const float RAW_STEREO_PAIR_DETUNE_SEMITONES = 0.07f;
static const float MASTER_VOLUME_MIN_DB = -56.0f;
static const float MASTER_VOLUME_MAX_DB = -3.0f;
static const float MASTER_VOLUME_MUTE_THRESHOLD = 0.005f;
static const float AMY_GLOBAL_VOLUME_UNITY = 10.0f;

static const float FILTER_CUTOFF_MIN_HZ = 20.0f;
static const float FILTER_CUTOFF_MAX_HZ = 20000.0f;
static const float SYNTH_REVERB_LEVEL = 0.28f;
static const float SYNTH_REVERB_LIVENESS = 0.85f;
static const float SYNTH_REVERB_DAMPING = 0.45f;
static const float SYNTH_REVERB_XOVER_HZ = 3000.0f;
static const float SYNTH_CHORUS_LEVEL = 0.30f;
static const float SYNTH_CHORUS_LFO_HZ = 0.55f;
static const float SYNTH_CHORUS_DEPTH = 0.55f;
static const float SYNTH_CHORUS_MAX_DELAY_SAMPLES = 320.0f;
static const float SYNTH_DELAY_LEVEL = 0.28f;
static const float SYNTH_DELAY_FEEDBACK = 0.32f;
static const float SYNTH_DELAY_FILTER_COEF = 0.28f;
static const float SYNTH_TREMOLO_MAX_DEPTH = 0.95f;
static const float LFO_MAX_PITCH_SEMITONES = 2.0f;
static const uint32_t ENVELOPE_ATTACK_MIN_MS = 1;
static const uint32_t ENVELOPE_ATTACK_MAX_MS = 2000;
static const uint32_t ENVELOPE_DECAY_MIN_MS = 1;
static const uint32_t ENVELOPE_DECAY_MAX_MS = 5000;
static const uint32_t ENVELOPE_RELEASE_MIN_MS = 1;
static const uint32_t ENVELOPE_RELEASE_MAX_MS = 5000;
static const uint16_t PORTAMENTO_MAX_MS = 1000;
static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const UBaseType_t AUDIO_TASK_PRIORITY = 7;
static const BaseType_t AUDIO_TASK_CORE = 1;

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_audio_task;
static SemaphoreHandle_t s_amy_graph_lock;
static bool s_initialized;
static amy_voice_t s_voices[AMY_MUSICAL_VOICE_COUNT];
static uint32_t s_voice_age;
static uint8_t s_voice_limit = AMY_MUSICAL_VOICE_COUNT;
static synth_amy_envelope_t s_envelope = {
    .attack_ms = 25,
    .decay_ms = 260,
    .sustain_level = 0.66f,
    .release_ms = 450,
};
static bool s_envelope_override_enabled;
static uint16_t s_portamento_ms;
static synth_sound_t s_sound = SYNTH_SOUND_SAW;
static bool s_patch_loaded;
static bool s_patch_pair_loaded;
static uint16_t s_patch_number;
static uint8_t s_patch_voice_count;
static uint8_t s_patch_oscs_per_voice;
static bool s_filter_enabled = true;
static float s_filter_cutoff_hz = FILTER_CUTOFF_MAX_HZ;
static bool s_lfo_enabled;
static volatile float s_lfo_pitch_octaves;
static bool s_tremolo_enabled;
static volatile float s_tremolo_amp_offset;
static bool s_stereo_enabled = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0;
#if ESP_POCKET_AUDIO_OUTPUT_DECLICK_ENABLED
static bool s_output_declick_ready;
static int16_t s_output_declick_last[AMY_NCHANS];
#endif

static float amy_external_coef(uint16_t channel)
{
    switch (channel) {
    case 0:
        return s_lfo_pitch_octaves;
    case 1:
        return s_tremolo_amp_offset;
    default:
        return 0.0f;
    }
}

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float db_to_gain(float db)
{
    return powf(10.0f, db / 20.0f);
}

static float master_volume_to_amy_volume(float normalized_volume)
{
    float normalized = clamp_float(normalized_volume, 0.0f, 1.0f);
    if (normalized <= MASTER_VOLUME_MUTE_THRESHOLD) {
        return 0.0f;
    }

    float shaped = sqrtf(normalized);
    float db = MASTER_VOLUME_MIN_DB + shaped * (MASTER_VOLUME_MAX_DB - MASTER_VOLUME_MIN_DB);
    return AMY_GLOBAL_VOLUME_UNITY * db_to_gain(db);
}

static void enqueue_master_volume(float normalized_volume)
{
    amy_event event = amy_default_event();
    event.volume = master_volume_to_amy_volume(normalized_volume);
    amy_add_event(&event);
}

static void amy_graph_lock(void)
{
    if (s_amy_graph_lock != NULL) {
        xSemaphoreTake(s_amy_graph_lock, portMAX_DELAY);
    }
}

static void amy_graph_unlock(void)
{
    if (s_amy_graph_lock != NULL) {
        xSemaphoreGive(s_amy_graph_lock);
    }
}

static void amy_hard_all_notes_off(void)
{
    amy_event event = amy_default_event();
    event.reset_osc = RESET_ALL_NOTES;
    amy_add_event(&event);
}

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

static const amy_sound_runtime_t *runtime_for_sound(synth_sound_t sound)
{
    return amy_runtime_for_sound(sound);
}

static const char *backend_name(amy_sound_backend_t backend)
{
    return amy_runtime_backend_name(backend);
}

static uint8_t runtime_voice_limit(const amy_sound_runtime_t *runtime)
{
    return amy_runtime_voice_limit(runtime, s_voice_limit);
}

static uint8_t main_osc_for_voice(uint8_t voice)
{
    return voice;
}

static uint8_t pair_osc_for_voice(uint8_t voice)
{
    return (uint8_t)(voice + AMY_STEREO_PAIR_OFFSET);
}

static bool runtime_supports_stereo_pairs(const amy_sound_runtime_t *runtime)
{
    return amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled);
}

static float main_pan_for_voice(uint8_t voice)
{
    if (!s_stereo_enabled || voice >= AMY_MUSICAL_VOICE_COUNT) {
        return 0.5f;
    }
    return amy_voice_map_main_pan(voice);
}

static float pair_pan_for_voice(uint8_t voice)
{
    return 1.0f - main_pan_for_voice(voice);
}

static float raw_amp_scale_for_runtime(const amy_sound_runtime_t *runtime)
{
    return runtime_supports_stereo_pairs(runtime) ? RAW_STEREO_PAIR_AMP_SCALE : 1.0f;
}

static uint16_t amy_config_max_oscs(void)
{
    uint16_t configured = ESP_POCKET_AUDIO_AMY_MAX_OSCS;
    if (ESP_POCKET_AUDIO_AMY_STEREO_PAIRS && configured < AMY_STEREO_MIN_OSCS) {
        return AMY_STEREO_MIN_OSCS;
    }
    return configured;
}

static uint32_t amy_config_max_voices(void)
{
    return AMY_MUSICAL_VOICE_COUNT * AMY_PATCH_SYNTH_COUNT;
}

static uint16_t active_osc_count(void)
{
    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    uint16_t active = 0;

    for (uint8_t voice = 0; voice < AMY_MUSICAL_VOICE_COUNT; ++voice) {
        if (s_voices[voice].active) {
            if (s_voices[voice].backend == AMY_SOUND_BACKEND_PATCH) {
                active = (uint16_t)(active + runtime->oscs_per_voice *
                                             (s_voices[voice].pair_active ? 2U : 1U));
            } else {
                active = (uint16_t)(active + 1U + (s_voices[voice].pair_active ? 1U : 0U));
            }
        }
    }
    return active;
}

static uint16_t allocated_patch_osc_count(void)
{
    if (!s_patch_loaded) {
        return 0;
    }
    uint16_t synth_count = (uint16_t)(1U + (s_patch_pair_loaded ? 1U : 0U));
    return (uint16_t)s_patch_voice_count * (uint16_t)s_patch_oscs_per_voice * synth_count;
}

static void set_amp_envelope(amy_event *event, const synth_amy_envelope_t *envelope)
{
    if (event == NULL || envelope == NULL) {
        return;
    }

    event->eg_type[0] = ENVELOPE_NORMAL;
    event->eg0_times[0] = envelope->attack_ms;
    event->eg0_values[0] = 1.0f;
    event->eg0_times[1] = envelope->decay_ms;
    event->eg0_values[1] = envelope->sustain_level;
    event->eg0_times[2] = envelope->release_ms;
    event->eg0_values[2] = 0.0f;
}

static void raw_profile_envelope(const amy_sound_runtime_t *runtime, synth_amy_envelope_t *envelope)
{
    if (runtime == NULL || envelope == NULL) {
        return;
    }

    envelope->attack_ms = runtime->raw.attack_ms;
    envelope->decay_ms = runtime->raw.decay_ms;
    envelope->sustain_level = runtime->raw.sustain_level;
    envelope->release_ms = runtime->raw.release_ms;
}

static const synth_amy_envelope_t *effective_raw_envelope(const amy_sound_runtime_t *runtime,
                                                          synth_amy_envelope_t *profile_envelope)
{
    if (s_envelope_override_enabled) {
        return &s_envelope;
    }

    raw_profile_envelope(runtime, profile_envelope);
    return profile_envelope;
}

static void fill_raw_filter_event(amy_event *event, const amy_raw_profile_t *profile)
{
    if (event == NULL || profile == NULL) {
        return;
    }

    bool profile_filter = profile->filter_cutoff_hz < FILTER_CUTOFF_MAX_HZ;
    if (s_filter_enabled || profile_filter) {
        float cutoff = profile->filter_cutoff_hz;
        if (s_filter_enabled) {
            cutoff = fminf(cutoff, s_filter_cutoff_hz);
        }
        event->filter_type = FILTER_LPF;
        event->filter_freq_coefs[COEF_CONST] =
            clamp_float(cutoff, FILTER_CUTOFF_MIN_HZ, FILTER_CUTOFF_MAX_HZ);
        event->resonance = s_filter_enabled ? 0.6f : profile->resonance;
    } else {
        event->filter_type = FILTER_NONE;
    }
}

static void amy_note_off_raw_osc(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.velocity = 0.0f;
    amy_add_event(&event);
}

static float raw_amp_for_voice(uint8_t voice, const amy_sound_runtime_t *runtime, float amp_scale)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL) {
        return 0.0f;
    }
    return RAW_NOTE_AMP * runtime->raw.amp * amy_voice_map_role_amp(voice) * amp_scale;
}

static void amy_note_on_raw_osc(uint8_t voice,
                                uint8_t osc,
                                uint8_t midi_note,
                                const amy_sound_runtime_t *runtime,
                                float pan,
                                float detune_semitones,
                                float amp_scale)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL) {
        return;
    }

    const amy_raw_profile_t *profile = &runtime->raw;
    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = profile->wave;
    if (runtime->backend == AMY_SOUND_BACKEND_PCM) {
        event.preset = runtime->pcm_preset;
        event.feedback = runtime->pcm_loop ? 1.0f : 0.0f;
    }
    event.midi_note = (float)midi_note + detune_semitones;
    event.velocity = clamp_float(runtime->velocity, 0.0f, 1.0f);
    event.amp_coefs[COEF_CONST] = raw_amp_for_voice(voice, runtime, amp_scale);
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    event.portamento_ms = s_portamento_ms;
    event.pan_coefs[COEF_CONST] = clamp_float(pan, 0.0f, 1.0f);
    event.duty_coefs[COEF_CONST] = profile->duty;
    fill_raw_filter_event(&event, profile);
    synth_amy_envelope_t profile_envelope = {0};
    set_amp_envelope(&event, effective_raw_envelope(runtime, &profile_envelope));
    amy_add_event(&event);
}

static void amy_note_on_raw(uint8_t voice, uint8_t midi_note, const amy_sound_runtime_t *runtime)
{
    float amp_scale = raw_amp_scale_for_runtime(runtime);
    amy_note_on_raw_osc(voice,
                        main_osc_for_voice(voice),
                        midi_note,
                        runtime,
                        main_pan_for_voice(voice),
                        0.0f,
                        amp_scale);

    s_voices[voice].pair_active = false;
    if (runtime_supports_stereo_pairs(runtime)) {
        amy_note_on_raw_osc(voice,
                            pair_osc_for_voice(voice),
                            midi_note,
                            runtime,
                            pair_pan_for_voice(voice),
                            RAW_STEREO_PAIR_DETUNE_SEMITONES,
                            amp_scale);
        s_voices[voice].pair_active = true;
    }
}

static void amy_note_on_patch_synth(uint8_t synth_id,
                                    float midi_note,
                                    const amy_sound_runtime_t *runtime)
{
    amy_event event = amy_default_event();
    event.synth = synth_id;
    event.midi_note = midi_note;
    float velocity = runtime != NULL ? runtime->velocity : 0.55f;
    event.velocity = clamp_float(velocity, 0.0f, 1.0f);
    event.portamento_ms = s_portamento_ms;
    amy_add_event(&event);
}

static bool amy_note_on_patch(uint8_t midi_note, const amy_sound_runtime_t *runtime)
{
    amy_note_on_patch_synth(AMY_PATCH_MAIN_SYNTH, (float)midi_note, runtime);
    if (runtime_supports_stereo_pairs(runtime) && s_patch_pair_loaded) {
        amy_note_on_patch_synth(AMY_PATCH_PAIR_SYNTH,
                                (float)midi_note + RAW_STEREO_PAIR_DETUNE_SEMITONES,
                                runtime);
        return true;
    }
    return false;
}

static void amy_note_off_patch_synth(uint8_t synth_id, uint8_t midi_note)
{
    amy_event event = amy_default_event();
    event.synth = synth_id;
    event.midi_note = midi_note;
    event.velocity = 0.0f;
    amy_add_event(&event);
}

static void amy_note_off_patch(uint8_t midi_note)
{
    amy_note_off_patch_synth(AMY_PATCH_MAIN_SYNTH, midi_note);
    if (s_patch_pair_loaded) {
        amy_note_off_patch_synth(AMY_PATCH_PAIR_SYNTH, midi_note);
    }
}

static void apply_raw_filter_osc(uint8_t osc, const amy_raw_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    amy_event event = amy_default_event();
    event.osc = osc;
    fill_raw_filter_event(&event, profile);
    amy_add_event(&event);
}

static void apply_raw_filter(uint8_t voice)
{
    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    if (voice >= AMY_MUSICAL_VOICE_COUNT ||
        (runtime->backend != AMY_SOUND_BACKEND_RAW && runtime->backend != AMY_SOUND_BACKEND_PCM)) {
        return;
    }

    apply_raw_filter_osc(main_osc_for_voice(voice), &runtime->raw);
    if (s_voices[voice].pair_active) {
        apply_raw_filter_osc(pair_osc_for_voice(voice), &runtime->raw);
    }
}

static void apply_raw_lfo_osc(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    amy_add_event(&event);
}

static void apply_raw_lfo(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    apply_raw_lfo_osc(main_osc_for_voice(voice));
    if (s_voices[voice].pair_active) {
        apply_raw_lfo_osc(pair_osc_for_voice(voice));
    }
}

static void apply_raw_envelope_osc(uint8_t osc, const synth_amy_envelope_t *envelope)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    set_amp_envelope(&event, envelope);
    amy_add_event(&event);
}

static void apply_raw_envelope(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }

    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    synth_amy_envelope_t profile_envelope = {0};
    const synth_amy_envelope_t *envelope = effective_raw_envelope(runtime, &profile_envelope);

    apply_raw_envelope_osc(main_osc_for_voice(voice), envelope);
    if (s_voices[voice].pair_active) {
        apply_raw_envelope_osc(pair_osc_for_voice(voice), envelope);
    }
}

static void apply_raw_portamento_osc(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.portamento_ms = s_portamento_ms;
    amy_add_event(&event);
}

static void apply_raw_portamento(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    apply_raw_portamento_osc(main_osc_for_voice(voice));
    if (s_voices[voice].pair_active) {
        apply_raw_portamento_osc(pair_osc_for_voice(voice));
    }
}

static void apply_raw_tremolo_osc(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    amy_add_event(&event);
}

static void apply_raw_tremolo(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    apply_raw_tremolo_osc(main_osc_for_voice(voice));
    if (s_voices[voice].pair_active) {
        apply_raw_tremolo_osc(pair_osc_for_voice(voice));
    }
}

static void apply_raw_stereo_state(uint8_t voice, const amy_sound_runtime_t *runtime)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL ||
        (runtime->backend != AMY_SOUND_BACKEND_RAW && runtime->backend != AMY_SOUND_BACKEND_PCM) ||
        !s_voices[voice].active) {
        return;
    }

    bool pairs_enabled = runtime_supports_stereo_pairs(runtime);
    float amp_scale = pairs_enabled ? RAW_STEREO_PAIR_AMP_SCALE : 1.0f;

    amy_event main_event = amy_default_event();
    main_event.osc = main_osc_for_voice(voice);
    main_event.amp_coefs[COEF_CONST] = raw_amp_for_voice(voice, runtime, amp_scale);
    main_event.pan_coefs[COEF_CONST] = pairs_enabled ? main_pan_for_voice(voice) : 0.5f;
    amy_add_event(&main_event);

    if (pairs_enabled) {
        if (!s_voices[voice].pair_active) {
            amy_note_on_raw_osc(voice,
                                pair_osc_for_voice(voice),
                                s_voices[voice].note,
                                runtime,
                                pair_pan_for_voice(voice),
                                RAW_STEREO_PAIR_DETUNE_SEMITONES,
                                amp_scale);
            s_voices[voice].pair_active = true;
        }
    } else if (s_voices[voice].pair_active) {
        amy_note_off_raw_osc(pair_osc_for_voice(voice));
        s_voices[voice].pair_active = false;
    }
}

static void apply_patch_pan_to_voice(uint16_t voice, float pan)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.voices[0] = voice;
        event.osc = osc;
        event.pan_coefs[COEF_CONST] = clamp_float(pan, 0.0f, 1.0f);
        amy_add_event(&event);
    }
}

static void apply_patch_pan_to_synth(uint8_t synth_id, bool pair)
{
    uint16_t voices[MAX_VOICES_PER_INSTRUMENT];
    int voice_count = instrument_get_num_voices(synth_id, voices);
    if (voice_count <= 0) {
        return;
    }

    for (uint8_t role = 0; role < (uint8_t)voice_count && role < AMY_MUSICAL_VOICE_COUNT; ++role) {
        float pan = s_stereo_enabled ?
            (pair ? pair_pan_for_voice(role) : main_pan_for_voice(role)) :
            0.5f;
        apply_patch_pan_to_voice(voices[role], pan);
    }
}

static void apply_patch_pan(void)
{
    if (!s_patch_loaded) {
        return;
    }
    apply_patch_pan_to_synth(AMY_PATCH_MAIN_SYNTH, false);
    if (s_patch_pair_loaded) {
        apply_patch_pan_to_synth(AMY_PATCH_PAIR_SYNTH, true);
    }
}

static void apply_patch_filter_to_synth(uint8_t synth_id)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.synth = synth_id;
        event.osc = osc;
        event.filter_type = FILTER_LPF;
        event.filter_freq_coefs[COEF_CONST] = s_filter_cutoff_hz;
        event.resonance = 0.55f;
        amy_add_event(&event);
    }
}

static bool patch_filter_override_active(void)
{
    return s_filter_enabled && s_filter_cutoff_hz < (FILTER_CUTOFF_MAX_HZ - 1.0f);
}

static void apply_patch_filter(void)
{
    if (!s_patch_loaded) {
        return;
    }

    apply_patch_filter_to_synth(AMY_PATCH_MAIN_SYNTH);
    if (s_patch_pair_loaded) {
        apply_patch_filter_to_synth(AMY_PATCH_PAIR_SYNTH);
    }
}

static void apply_patch_lfo_to_synth(uint8_t synth_id)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.synth = synth_id;
        event.osc = osc;
        event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
        amy_add_event(&event);
    }
}

static void apply_patch_lfo(void)
{
    if (!s_patch_loaded) {
        return;
    }

    apply_patch_lfo_to_synth(AMY_PATCH_MAIN_SYNTH);
    if (s_patch_pair_loaded) {
        apply_patch_lfo_to_synth(AMY_PATCH_PAIR_SYNTH);
    }
}

static void apply_patch_tremolo_to_synth(uint8_t synth_id)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.synth = synth_id;
        event.osc = osc;
        event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
        amy_add_event(&event);
    }
}

static void apply_patch_tremolo(void)
{
    if (!s_patch_loaded) {
        return;
    }

    apply_patch_tremolo_to_synth(AMY_PATCH_MAIN_SYNTH);
    if (s_patch_pair_loaded) {
        apply_patch_tremolo_to_synth(AMY_PATCH_PAIR_SYNTH);
    }
}

static void apply_patch_envelope_to_synth(uint8_t synth_id)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.synth = synth_id;
        event.osc = osc;
        set_amp_envelope(&event, &s_envelope);
        amy_add_event(&event);
    }
}

static void apply_patch_envelope(void)
{
    if (!s_patch_loaded) {
        return;
    }

    apply_patch_envelope_to_synth(AMY_PATCH_MAIN_SYNTH);
    if (s_patch_pair_loaded) {
        apply_patch_envelope_to_synth(AMY_PATCH_PAIR_SYNTH);
    }
}

static void apply_patch_portamento_to_synth(uint8_t synth_id)
{
    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        amy_event event = amy_default_event();
        event.synth = synth_id;
        event.osc = osc;
        event.portamento_ms = s_portamento_ms;
        amy_add_event(&event);
    }
}

static void apply_patch_portamento(void)
{
    if (!s_patch_loaded) {
        return;
    }

    apply_patch_portamento_to_synth(AMY_PATCH_MAIN_SYNTH);
    if (s_patch_pair_loaded) {
        apply_patch_portamento_to_synth(AMY_PATCH_PAIR_SYNTH);
    }
}

static void apply_patch_runtime_controls(bool force_filter)
{
    if (!s_patch_loaded) {
        return;
    }

    if (s_envelope_override_enabled) {
        apply_patch_envelope();
    }
    apply_patch_portamento();
    if (force_filter || patch_filter_override_active()) {
        apply_patch_filter();
    }
    if (s_lfo_enabled) {
        apply_patch_lfo();
    }
    if (s_tremolo_enabled) {
        apply_patch_tremolo();
    }
}

static void release_voice(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || !s_voices[voice].active) {
        return;
    }

    if (s_voices[voice].backend == AMY_SOUND_BACKEND_PATCH) {
        amy_note_off_patch(s_voices[voice].note);
    } else {
        amy_note_off_raw_osc(main_osc_for_voice(voice));
        if (s_voices[voice].pair_active) {
            amy_note_off_raw_osc(pair_osc_for_voice(voice));
        }
    }

    s_voices[voice].pair_active = false;
    s_voices[voice].active = false;
}

static void release_slot_voices(uint8_t slot)
{
    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].active && s_voices[i].slot == slot) {
            release_voice(i);
        }
    }
}

static void configure_patch_synth_instance(uint8_t synth_id, const amy_sound_runtime_t *runtime)
{
    if (runtime == NULL || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    amy_event event = amy_default_event();
    event.synth = synth_id;
    event.patch_number = runtime->patch_number;
    event.num_voices = runtime_voice_limit(runtime);
    amy_add_event(&event);
}

static void configure_patch_synth(const amy_sound_runtime_t *runtime)
{
    if (!s_initialized || runtime == NULL || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    uint8_t voices = runtime_voice_limit(runtime);
    configure_patch_synth_instance(AMY_PATCH_MAIN_SYNTH, runtime);

    s_patch_loaded = true;
    s_patch_pair_loaded = false;
    s_patch_number = runtime->patch_number;
    s_patch_voice_count = voices;
    s_patch_oscs_per_voice = runtime->oscs_per_voice;

    if (runtime_supports_stereo_pairs(runtime)) {
        configure_patch_synth_instance(AMY_PATCH_PAIR_SYNTH, runtime);
        s_patch_pair_loaded = true;
    }

    apply_patch_pan();
    apply_patch_runtime_controls(false);
    ESP_LOGI(TAG,
             "patch synths=%u patch=%u %s voices=%u oscs_per_voice=%u alloc_oscs=%u",
             (unsigned)(1U + (s_patch_pair_loaded ? 1U : 0U)),
             runtime->patch_number,
             runtime->patch_label != NULL ? runtime->patch_label : "",
             voices,
             runtime->oscs_per_voice,
             (unsigned)allocated_patch_osc_count());
}

static void replay_active_patch_voices(const amy_sound_runtime_t *runtime)
{
    if (runtime == NULL || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].active && s_voices[i].backend == AMY_SOUND_BACKEND_PATCH) {
            s_voices[i].pair_active = amy_note_on_patch(s_voices[i].note, runtime);
        }
    }
}

static void reload_patch_synth_preserving_notes(void)
{
    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    if (!s_patch_loaded || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    configure_patch_synth(runtime);
    replay_active_patch_voices(runtime);
}

static void release_patch_synth_instance(uint8_t synth_id)
{
    amy_event event = amy_default_event();
    event.synth = synth_id;
    event.num_voices = 0;
    amy_add_event(&event);
}

static void release_patch_synth(void)
{
    if (!s_initialized || !s_patch_loaded) {
        return;
    }

    if (s_patch_pair_loaded) {
        release_patch_synth_instance(AMY_PATCH_PAIR_SYNTH);
    }
    release_patch_synth_instance(AMY_PATCH_MAIN_SYNTH);

    s_patch_loaded = false;
    s_patch_pair_loaded = false;
    s_patch_number = 0;
    s_patch_voice_count = 0;
    s_patch_oscs_per_voice = 0;
}

esp_err_t synth_amy_set_sound(synth_sound_t sound)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    const amy_sound_runtime_t *runtime = runtime_for_sound(sound);
    if (s_initialized && sound == s_sound &&
        (runtime->backend != AMY_SOUND_BACKEND_PATCH || s_patch_loaded)) {
        return ESP_OK;
    }

    if (s_initialized) {
        amy_graph_lock();
    }

    if (s_initialized) {
        esp_err_t err = synth_amy_all_off();
        if (err != ESP_OK) {
            amy_graph_unlock();
            ESP_LOGE(TAG, "all off failed: %s", esp_err_to_name(err));
            return err;
        }
        amy_hard_all_notes_off();
        release_patch_synth();
    }

    s_sound = sound;

    if (s_initialized && runtime->backend == AMY_SOUND_BACKEND_PATCH) {
        configure_patch_synth(runtime);
    }

    if (s_initialized) {
        amy_graph_unlock();
    }

    ESP_LOGI(TAG,
             "sound=%s engine=%s backend=%s velocity=%.2f%s%s",
             synth_sound_name(sound),
             synth_sound_engine_name(sound),
             backend_name(runtime->backend),
             runtime->velocity,
             runtime->patch_label != NULL ? " patch=" : "",
             runtime->patch_label != NULL ? runtime->patch_label : "");
    return ESP_OK;
}

float synth_amy_sound_velocity(synth_sound_t sound)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return 0.0f;
    }
    return runtime_for_sound(sound)->velocity;
}

bool synth_amy_sound_patch_info(synth_sound_t sound, uint16_t *patch_number, const char **patch_label)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return false;
    }

    const amy_sound_runtime_t *runtime = runtime_for_sound(sound);
    if (runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return false;
    }

    if (patch_number != NULL) {
        *patch_number = runtime->patch_number;
    }
    if (patch_label != NULL) {
        *patch_label = runtime->patch_label;
    }
    return true;
}

esp_err_t synth_amy_set_builtin_patch(uint16_t patch_number,
                                      uint8_t voices,
                                      uint8_t oscs_per_voice)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (patch_number >= SYNTH_AMY_BUILTIN_PATCH_COUNT || voices == 0 || oscs_per_voice == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    amy_graph_lock();
    esp_err_t err = synth_amy_all_off();
    if (err != ESP_OK) {
        amy_graph_unlock();
        ESP_LOGE(TAG, "all off failed: %s", esp_err_to_name(err));
        return err;
    }
    amy_hard_all_notes_off();
    release_patch_synth();

    amy_sound_runtime_t runtime = {
        .backend = AMY_SOUND_BACKEND_PATCH,
        .patch_number = patch_number,
        .patch_voices = voices,
        .oscs_per_voice = oscs_per_voice,
        .velocity = 0.55f,
        .patch_label = "diagnostic",
    };
    configure_patch_synth(&runtime);
    amy_graph_unlock();
    return ESP_OK;
}

esp_err_t synth_amy_builtin_patch_note_on(uint8_t midi_note, float velocity)
{
    if (!s_initialized || !s_patch_loaded) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_sound_runtime_t runtime = {
        .backend = AMY_SOUND_BACKEND_PATCH,
        .patch_number = s_patch_number,
        .patch_voices = s_patch_voice_count,
        .oscs_per_voice = s_patch_oscs_per_voice,
        .velocity = velocity,
        .patch_label = "diagnostic",
    };
    amy_note_on_patch(midi_note, &runtime);
    return ESP_OK;
}

esp_err_t synth_amy_builtin_patch_note_off(uint8_t midi_note)
{
    if (!s_initialized || !s_patch_loaded) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_note_off_patch(midi_note);
    return ESP_OK;
}

esp_err_t synth_amy_set_volume(float normalized_volume)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    enqueue_master_volume(normalized_volume);
    return ESP_OK;
}

esp_err_t synth_amy_set_filter_enabled(bool enabled)
{
    bool old_patch_override = patch_filter_override_active();
    s_filter_enabled = enabled;
    bool new_patch_override = patch_filter_override_active();
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        if (new_patch_override) {
            apply_patch_filter();
        } else if (old_patch_override) {
            reload_patch_synth_preserving_notes();
        }
        return ESP_OK;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].active) {
            apply_raw_filter(i);
        }
    }
    return ESP_OK;
}

esp_err_t synth_amy_set_filter_cutoff(float cutoff_hz)
{
    bool old_patch_override = patch_filter_override_active();
    s_filter_cutoff_hz = clamp_float(cutoff_hz, FILTER_CUTOFF_MIN_HZ, FILTER_CUTOFF_MAX_HZ);
    bool new_patch_override = patch_filter_override_active();
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        if (new_patch_override) {
            apply_patch_filter();
        } else if (old_patch_override) {
            reload_patch_synth_preserving_notes();
        }
        return ESP_OK;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].active) {
            apply_raw_filter(i);
        }
    }
    return ESP_OK;
}

esp_err_t synth_amy_set_reverb_enabled(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.reverb_level = enabled ? SYNTH_REVERB_LEVEL : 0.0f;
    if (enabled) {
        event.reverb_liveness = SYNTH_REVERB_LIVENESS;
        event.reverb_damping = SYNTH_REVERB_DAMPING;
        event.reverb_xover_hz = SYNTH_REVERB_XOVER_HZ;
    }
    amy_add_event(&event);
    ESP_LOGI(TAG, "reverb %s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_set_chorus_enabled(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.chorus_level = enabled ? SYNTH_CHORUS_LEVEL : 0.0f;
    if (enabled) {
        event.chorus_max_delay = SYNTH_CHORUS_MAX_DELAY_SAMPLES;
        event.chorus_lfo_freq = SYNTH_CHORUS_LFO_HZ;
        event.chorus_depth = SYNTH_CHORUS_DEPTH;
    }
    amy_add_event(&event);
    ESP_LOGI(TAG, "chorus %s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_set_delay_enabled(bool enabled, float delay_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    delay_ms = clamp_float(delay_ms, 1.0f, SYNTH_DELAY_MAX_MS);
    amy_event event = amy_default_event();
    event.echo_max_delay_ms = SYNTH_DELAY_MAX_MS;
    event.echo_level = enabled ? SYNTH_DELAY_LEVEL : 0.0f;
    if (enabled) {
        event.echo_delay_ms = delay_ms;
        event.echo_feedback = SYNTH_DELAY_FEEDBACK;
        event.echo_filter_coef = SYNTH_DELAY_FILTER_COEF;
    }
    amy_add_event(&event);
    ESP_LOGI(TAG, "delay %s %.1f ms", enabled ? "on" : "off", delay_ms);
    return ESP_OK;
}

esp_err_t synth_amy_set_tremolo_enabled(bool enabled)
{
    s_tremolo_enabled = enabled;
    if (!enabled) {
        s_tremolo_amp_offset = 0.0f;
    }
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        apply_patch_tremolo();
    } else {
        for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
            if (s_voices[i].active) {
                apply_raw_tremolo(i);
            }
        }
    }
    ESP_LOGI(TAG, "tremolo %s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_set_tremolo_amount(float amount)
{
    amount = clamp_float(amount, 0.0f, SYNTH_TREMOLO_MAX_DEPTH);
    s_tremolo_amp_offset = -amount;
    return ESP_OK;
}

esp_err_t synth_amy_set_flanger_enabled(bool enabled)
{
    synth_amy_flanger_set_enabled(enabled);
    ESP_LOGI(TAG, "flanger %s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_set_stereo_enabled(bool enabled)
{
    s_stereo_enabled = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0 && enabled;
    if (!s_initialized) {
        return ESP_OK;
    }

    amy_graph_lock();
    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH) {
        if (s_patch_loaded && s_stereo_enabled && !s_patch_pair_loaded) {
            configure_patch_synth_instance(AMY_PATCH_PAIR_SYNTH, runtime);
            s_patch_pair_loaded = true;
            apply_patch_pan();
            apply_patch_runtime_controls(false);
            for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
                if (s_voices[i].active && s_voices[i].backend == AMY_SOUND_BACKEND_PATCH) {
                    amy_note_on_patch_synth(AMY_PATCH_PAIR_SYNTH,
                                            (float)s_voices[i].note + RAW_STEREO_PAIR_DETUNE_SEMITONES,
                                            runtime);
                    s_voices[i].pair_active = true;
                }
            }
        } else if (s_patch_loaded && !s_stereo_enabled && s_patch_pair_loaded) {
            release_patch_synth_instance(AMY_PATCH_PAIR_SYNTH);
            s_patch_pair_loaded = false;
            for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
                if (s_voices[i].backend == AMY_SOUND_BACKEND_PATCH) {
                    s_voices[i].pair_active = false;
                }
            }
            apply_patch_pan();
        } else if (s_patch_loaded) {
            apply_patch_pan();
        }
    } else {
        for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
            apply_raw_stereo_state(i, runtime);
        }
    }
    amy_graph_unlock();

    ESP_LOGI(TAG,
             "stereo pairs %s backend=%s detune=%.1f cents alloc_oscs=%u",
             s_stereo_enabled ? "on" : "off",
             backend_name(runtime->backend),
             s_stereo_enabled ? RAW_STEREO_PAIR_DETUNE_SEMITONES * 100.0f : 0.0f,
             (unsigned)allocated_patch_osc_count());
    return ESP_OK;
}

esp_err_t synth_amy_set_lfo_enabled(bool enabled)
{
    s_lfo_enabled = enabled;
    if (!enabled) {
        s_lfo_pitch_octaves = 0.0f;
    }
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        apply_patch_lfo();
    } else {
        for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
            if (s_voices[i].active) {
                apply_raw_lfo(i);
            }
        }
    }
    ESP_LOGI(TAG, "pitch lfo %s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_set_lfo_pitch(float semitones)
{
    semitones = clamp_float(semitones, -LFO_MAX_PITCH_SEMITONES, LFO_MAX_PITCH_SEMITONES);
    s_lfo_pitch_octaves = semitones / 12.0f;
    return ESP_OK;
}

esp_err_t synth_amy_set_voice_limit(uint8_t voices)
{
    uint8_t limit = clamp_voice_count(voices);
    if (s_voice_limit == limit) {
        return ESP_OK;
    }

    s_voice_limit = limit;
    if (!s_initialized) {
        return ESP_OK;
    }

    amy_graph_lock();
    esp_err_t err = synth_amy_all_off();
    if (err == ESP_OK && s_patch_loaded) {
        configure_patch_synth(runtime_for_sound(s_sound));
    }
    amy_graph_unlock();

    ESP_LOGI(TAG, "voice limit=%u", (unsigned)s_voice_limit);
    return err;
}

esp_err_t synth_amy_set_envelope(const synth_amy_envelope_t *envelope)
{
    if (envelope == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    synth_amy_envelope_t next = {
        .attack_ms = clamp_u32(envelope->attack_ms,
                               ENVELOPE_ATTACK_MIN_MS,
                               ENVELOPE_ATTACK_MAX_MS),
        .decay_ms = clamp_u32(envelope->decay_ms,
                              ENVELOPE_DECAY_MIN_MS,
                              ENVELOPE_DECAY_MAX_MS),
        .sustain_level = clamp_float(envelope->sustain_level, 0.0f, 1.0f),
        .release_ms = clamp_u32(envelope->release_ms,
                                ENVELOPE_RELEASE_MIN_MS,
                                ENVELOPE_RELEASE_MAX_MS),
    };

    s_envelope = next;
    s_envelope_override_enabled = true;

    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        apply_patch_envelope();
    } else {
        for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
            if (s_voices[i].active) {
                apply_raw_envelope(i);
            }
        }
    }

    ESP_LOGI(TAG,
             "envelope attack=%lu decay=%lu sustain=%.2f release=%lu",
             (unsigned long)s_envelope.attack_ms,
             (unsigned long)s_envelope.decay_ms,
             s_envelope.sustain_level,
             (unsigned long)s_envelope.release_ms);
    return ESP_OK;
}

esp_err_t synth_amy_set_portamento(uint16_t portamento_ms)
{
    if (portamento_ms > PORTAMENTO_MAX_MS) {
        portamento_ms = PORTAMENTO_MAX_MS;
    }
    if (s_portamento_ms == portamento_ms) {
        return ESP_OK;
    }

    s_portamento_ms = portamento_ms;
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_patch_loaded) {
        apply_patch_portamento();
    } else {
        for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
            if (s_voices[i].active) {
                apply_raw_portamento(i);
            }
        }
    }

    ESP_LOGI(TAG, "portamento=%ums", (unsigned)s_portamento_ms);
    return ESP_OK;
}

static void play_voice(uint8_t slot, uint8_t role, uint8_t note, const amy_sound_runtime_t *runtime)
{
    if (role >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL) {
        return;
    }

    release_voice(role);

    bool pair_active = false;
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH) {
        pair_active = amy_note_on_patch(note, runtime);
    } else {
        amy_note_on_raw(role, note, runtime);
        pair_active = s_voices[role].pair_active;
    }

    s_voices[role].active = true;
    s_voices[role].pair_active = pair_active;
    s_voices[role].backend = runtime->backend;
    s_voices[role].slot = slot;
    s_voices[role].note = note;
    s_voices[role].age = ++s_voice_age;
}

#if ESP_POCKET_AUDIO_OUTPUT_DECLICK_ENABLED
static void output_declick_process(int16_t *block, uint32_t frames, uint8_t channels)
{
    if (block == NULL || frames == 0 || channels == 0 || channels > AMY_NCHANS) {
        return;
    }

    uint32_t fade_frames = ESP_POCKET_AUDIO_OUTPUT_DECLICK_FRAMES;
    if (fade_frames == 0) {
        return;
    }
    if (fade_frames > frames) {
        fade_frames = frames;
    }

    if (s_output_declick_ready) {
        for (uint32_t frame = 0; frame < fade_frames; ++frame) {
            int32_t mix = (int32_t)(frame + 1U);
            int32_t denom = (int32_t)(fade_frames + 1U);
            for (uint8_t channel = 0; channel < channels; ++channel) {
                uint32_t index = frame * channels + channel;
                int32_t previous = s_output_declick_last[channel];
                int32_t current = block[index];
                block[index] = (int16_t)(previous + (((current - previous) * mix) / denom));
            }
        }
    }

    uint32_t last_frame = frames - 1U;
    for (uint8_t channel = 0; channel < channels; ++channel) {
        s_output_declick_last[channel] = block[last_frame * channels + channel];
    }
    s_output_declick_ready = true;
}
#endif

static void audio_task(void *arg)
{
    (void)arg;

    while (true) {
        int64_t render_start_us = esp_timer_get_time();
        amy_graph_lock();
        int16_t *block = amy_update();
        amy_graph_unlock();
        synth_amy_flanger_process(block);
        synth_looper_process_audio(block, AMY_BLOCK_SIZE, AMY_NCHANS);
#if ESP_POCKET_AUDIO_OUTPUT_DECLICK_ENABLED
        output_declick_process(block, AMY_BLOCK_SIZE, AMY_NCHANS);
#endif
        int64_t render_done_us = esp_timer_get_time();
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_hw->i2s_tx,
            block,
            AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
        int64_t write_done_us = esp_timer_get_time();
        synth_amy_perf_record_block((uint32_t)(render_done_us - render_start_us),
                                    (uint32_t)(write_done_us - render_done_us),
                                    write_done_us,
                                    active_osc_count(),
                                    allocated_patch_osc_count(),
                                    SYNTH_AUDIO_BLOCK_US);
        if (err != ESP_OK || bytes_written != AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t)) {
            ESP_LOGW(TAG, "AMY I2S write failed: %s bytes=%u",
                     esp_err_to_name(err),
                     (unsigned)bytes_written);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

esp_err_t synth_amy_init(const esp_pocket_audio_hw_t *hw)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_hw = hw;

    amy_config_t config = amy_default_config();
    config.audio = AMY_AUDIO_IS_NONE;
    config.midi = AMY_MIDI_IS_NONE;
    config.features.default_synths = 0;
    config.features.startup_bleep = 0;
    config.features.reverb = 1;
    config.features.chorus = 1;
    config.features.echo = 1;
    config.features.partials = 1;
    config.features.custom = 0;
    config.platform.multicore = ESP_POCKET_AUDIO_AMY_MULTICORE;
    config.platform.multithread = 0;
    config.max_oscs = amy_config_max_oscs();
    config.max_voices = amy_config_max_voices();
    config.max_synths = AMY_PATCH_SYNTH_COUNT;
    config.max_memory_patches = 4;
    config.max_sequencer_tags = 8;
    config.amy_external_coef_hook = amy_external_coef;
    amy_start(config);
    enqueue_master_volume((float)ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT / 100.0f);
    amy_global.echo.max_delay_samples = SYNTH_DELAY_MAX_SAMPLES;
    synth_amy_perf_reset();
    ESP_RETURN_ON_ERROR(synth_looper_init(AMY_SAMPLE_RATE, AMY_NCHANS),
                        TAG,
                        "looper init failed");
    s_amy_graph_lock = xSemaphoreCreateMutex();
    if (s_amy_graph_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        audio_task,
        "amy_audio",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AUDIO_TASK_PRIORITY,
        &s_audio_task,
        AUDIO_TASK_CORE
    );
    if (task_created != pdPASS) {
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "AMY synth started: %d Hz, %d channels, block=%d (%u us), max_oscs=%u synth_voices=%u juno=%u dx7=%u piano=%u pcm=%u stereo_pairs=%s multicore=%s perf_log=%s",
             AMY_SAMPLE_RATE,
             AMY_NCHANS,
             AMY_BLOCK_SIZE,
             SYNTH_AUDIO_BLOCK_US,
             (unsigned)config.max_oscs,
             (unsigned)config.max_voices,
             (unsigned)ESP_POCKET_AUDIO_AMY_JUNO_VOICES,
             (unsigned)ESP_POCKET_AUDIO_AMY_DX7_VOICES,
             (unsigned)ESP_POCKET_AUDIO_AMY_PIANO_VOICES,
             (unsigned)ESP_POCKET_AUDIO_AMY_PCM_VOICES,
             s_stereo_enabled ? "on" : "off",
             config.platform.multicore ? "on" : "off",
             ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_chord_off(uint8_t slot)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    release_slot_voices(slot);
    return ESP_OK;
}

esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slot >= SYNTH_CHORD_BUTTON_COUNT || chord == NULL || chord->note_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    release_slot_voices(slot);

    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH && !s_patch_loaded) {
        configure_patch_synth(runtime);
    }

    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT];
    amy_voice_map_build_assignment(chord, assignments);

    const uint8_t *priority = amy_voice_map_role_priority(runtime->backend);
    uint8_t limit = runtime_voice_limit(runtime);
    uint8_t played = 0;
#if ESP_POCKET_AUDIO_AMY_VOICE_MAP_LOG_ENABLED
    char assignment_log[224] = {0};
    size_t used = 0;
#endif

    for (uint8_t index = 0; index < AMY_MUSICAL_VOICE_COUNT; ++index) {
        uint8_t role = priority[index];
        if (role >= AMY_MUSICAL_VOICE_COUNT) {
            continue;
        }
        if (!assignments[role].enabled) {
            continue;
        }
        if (played >= limit) {
#if ESP_POCKET_AUDIO_AMY_VOICE_MAP_LOG_ENABLED
            amy_voice_map_append_note_log(assignment_log,
                                          sizeof(assignment_log),
                                          &used,
                                          "skip-",
                                          role,
                                          assignments[role].note);
#endif
            continue;
        }
        play_voice(slot, role, assignments[role].note, runtime);
#if ESP_POCKET_AUDIO_AMY_VOICE_MAP_LOG_ENABLED
        amy_voice_map_append_note_log(assignment_log,
                                      sizeof(assignment_log),
                                      &used,
                                      "",
                                      role,
                                      assignments[role].note);
#endif
        ++played;
    }

#if ESP_POCKET_AUDIO_AMY_VOICE_MAP_LOG_ENABLED
    ESP_LOGI(TAG,
             "voice map slot=%u sound=%s engine=%s backend=%s voices=%u/%u active_oscs=%u alloc_oscs=%u %s",
             slot + 1,
             synth_sound_name(s_sound),
             synth_sound_engine_name(s_sound),
             backend_name(runtime->backend),
             played,
             limit,
             (unsigned)active_osc_count(),
             (unsigned)allocated_patch_osc_count(),
             assignment_log);
#endif
    return ESP_OK;
}

esp_err_t synth_amy_chord_voice_on(uint8_t slot, const synth_chord_t *chord, uint8_t voice_index)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slot >= SYNTH_CHORD_BUTTON_COUNT || chord == NULL || chord->note_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const amy_sound_runtime_t *runtime = runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH && !s_patch_loaded) {
        configure_patch_synth(runtime);
    }

    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT];
    amy_voice_map_build_assignment(chord, assignments);

    const uint8_t *priority = amy_voice_map_role_priority(runtime->backend);
    uint8_t limit = runtime_voice_limit(runtime);
    uint8_t played = 0;
    uint8_t available = 0;

    for (uint8_t index = 0; index < AMY_MUSICAL_VOICE_COUNT; ++index) {
        uint8_t role = priority[index];
        if (role >= AMY_MUSICAL_VOICE_COUNT || !assignments[role].enabled) {
            continue;
        }
        if (available == voice_index) {
            if (played >= limit) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            play_voice(slot, role, assignments[role].note, runtime);
            ESP_LOGD(TAG,
                     "voice on slot=%u role=%s note=%u active_oscs=%u alloc_oscs=%u",
                     slot + 1,
                     amy_voice_map_role_name(role),
                     assignments[role].note,
                     (unsigned)active_osc_count(),
                     (unsigned)allocated_patch_osc_count());
            return ESP_OK;
        }
        ++available;
        if (played < limit) {
            ++played;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t synth_amy_all_off(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        release_voice(i);
    }
    return ESP_OK;
}
