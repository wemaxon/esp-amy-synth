#include "synth/amy_engine.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "amy.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "synth/amy_audio.h"
#include "synth/amy_event_builder.h"
#include "synth/amy_flanger.h"
#include "synth/amy_perf.h"
#include "synth/amy_runtime.h"
#include "synth/amy_voice_map.h"
#include "synth/looper.h"
#include "synth/mic_sample.h"
#include "synth/level_calibration.h"


#define SYNTH_DELAY_MAX_SAMPLES 32768U
#define SYNTH_DELAY_MAX_MS      (((float)SYNTH_DELAY_MAX_SAMPLES * 1000.0f) / (float)AMY_SAMPLE_RATE)

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
static const uint8_t SYNTH_AMY_DRUM_OSC_COUNT = 8;
static const uint16_t AMY_DRUM_OSC_RESERVATION_OWNER = UINT16_MAX - 1U;
static const float SYNTH_AMY_DRUM_AMP = 0.56f;
static const uint32_t ENVELOPE_ATTACK_MIN_MS = 1;
static const uint32_t ENVELOPE_ATTACK_MAX_MS = 2000;
static const uint32_t ENVELOPE_DECAY_MIN_MS = 1;
static const uint32_t ENVELOPE_DECAY_MAX_MS = 5000;
static const uint32_t ENVELOPE_RELEASE_MIN_MS = 1;
static const uint32_t ENVELOPE_RELEASE_MAX_MS = 5000;
static const uint16_t PORTAMENTO_MAX_MS = 1000;

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
static uint16_t s_drum_osc_base;
static uint8_t s_drum_next_osc;
static uint16_t s_patch_number;
static uint8_t s_patch_voice_count;
static uint8_t s_patch_oscs_per_voice;
static float s_patch_level_gain = 1.0f;
static bool s_filter_enabled = true;
static float s_filter_cutoff_hz = FILTER_CUTOFF_MAX_HZ;
static bool s_lfo_enabled;
static volatile float s_lfo_pitch_octaves;
static bool s_tremolo_enabled;
static volatile float s_tremolo_amp_offset;
static bool s_stereo_enabled = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0;
static bool s_mic_sample_ready;
static uint16_t s_mic_sample_preset = 1024U;

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

static uint8_t main_osc_for_voice(uint8_t voice)
{
    return voice;
}

static uint8_t pair_osc_for_voice(uint8_t voice)
{
    return (uint8_t)(voice + AMY_STEREO_PAIR_OFFSET);
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
    return amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled) ?
               RAW_STEREO_PAIR_AMP_SCALE : 1.0f;
}

static uint16_t patch_osc_capacity(uint8_t voices, uint8_t oscs_per_voice)
{
    return (uint16_t)voices * (uint16_t)oscs_per_voice * (uint16_t)AMY_PATCH_SYNTH_COUNT;
}

static uint16_t required_patch_osc_capacity(void)
{
    uint16_t max_oscs = patch_osc_capacity(ESP_POCKET_AUDIO_AMY_JUNO_VOICES,
                                           AMY_JUNO_OSCS_PER_VOICE);
    uint16_t dx7_oscs = patch_osc_capacity(ESP_POCKET_AUDIO_AMY_DX7_VOICES,
                                           AMY_DX7_OSCS_PER_VOICE);
    if (dx7_oscs > max_oscs) {
        max_oscs = dx7_oscs;
    }
    return (uint16_t)(max_oscs + SYNTH_AMY_DRUM_OSC_COUNT + 20U);
}

static uint16_t amy_config_max_oscs(void)
{
    uint16_t configured = ESP_POCKET_AUDIO_AMY_MAX_OSCS;
    uint16_t required = required_patch_osc_capacity();
    if (configured < required) {
        return required;
    }
    return configured;
}

static uint32_t amy_config_max_voices(void)
{
    return AMY_MUSICAL_VOICE_COUNT * AMY_PATCH_SYNTH_COUNT;
}

static uint16_t active_osc_count(void)
{
    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
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
    uint16_t allocated = SYNTH_AMY_DRUM_OSC_COUNT;
    if (!s_patch_loaded) {
        return allocated;
    }
    uint16_t synth_count = (uint16_t)(1U + (s_patch_pair_loaded ? 1U : 0U));
    return (uint16_t)(allocated +
                      ((uint16_t)s_patch_voice_count * (uint16_t)s_patch_oscs_per_voice * synth_count));
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

static amy_filter_controls_t current_filter_controls(void)
{
    return (amy_filter_controls_t){
        .enabled = s_filter_enabled,
        .cutoff_hz = s_filter_cutoff_hz,
        .minimum_hz = FILTER_CUTOFF_MIN_HZ,
        .maximum_hz = FILTER_CUTOFF_MAX_HZ,
    };
}

static void amy_note_off_raw_osc(uint16_t osc)
{
    amy_event event;
    amy_event_build_raw_note_off(&event, osc);
    amy_add_event(&event);
}

static float raw_amp_for_voice(uint8_t voice, const amy_sound_runtime_t *runtime, float amp_scale)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL) {
        return 0.0f;
    }
    return RAW_NOTE_AMP * runtime->raw.amp * amy_voice_map_role_amp(voice) * amp_scale *
           synth_level_calibration_sound_gain(s_sound);
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

    synth_amy_envelope_t profile_envelope = {0};
    amy_raw_note_event_config_t config = {
        .amplitude = raw_amp_for_voice(voice, runtime, amp_scale),
        .pan = pan,
        .detune_semitones = detune_semitones,
        .tremolo_enabled = s_tremolo_enabled,
        .lfo_enabled = s_lfo_enabled,
        .portamento_ms = s_portamento_ms,
        .filter = current_filter_controls(),
        .envelope = effective_raw_envelope(runtime, &profile_envelope),
    };
    amy_sound_runtime_t effective_runtime = *runtime;
    if (s_sound == SYNTH_SOUND_MIC_SAMPLE && runtime->backend == AMY_SOUND_BACKEND_PCM) {
        effective_runtime.pcm_preset = (int16_t)s_mic_sample_preset;
    }
    amy_event event;
    amy_event_build_raw_note(&event, osc, midi_note, &effective_runtime, &config);
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
    if (amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled)) {
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
    amy_event event;
    amy_event_build_patch_note(&event, synth_id, midi_note, runtime, s_portamento_ms);
    amy_add_event(&event);
}

static bool amy_note_on_patch(uint8_t midi_note, const amy_sound_runtime_t *runtime)
{
    amy_note_on_patch_synth(AMY_PATCH_MAIN_SYNTH, (float)midi_note, runtime);
    if (amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled) && s_patch_pair_loaded) {
        amy_note_on_patch_synth(AMY_PATCH_PAIR_SYNTH,
                                (float)midi_note + RAW_STEREO_PAIR_DETUNE_SEMITONES,
                                runtime);
        return true;
    }
    return false;
}

static void amy_note_off_patch_synth(uint8_t synth_id, uint8_t midi_note)
{
    amy_event event;
    amy_event_build_patch_note_off(&event, synth_id, midi_note);
    amy_add_event(&event);
}

static void amy_note_off_patch(uint8_t midi_note)
{
    amy_note_off_patch_synth(AMY_PATCH_MAIN_SYNTH, midi_note);
    if (s_patch_pair_loaded) {
        amy_note_off_patch_synth(AMY_PATCH_PAIR_SYNTH, midi_note);
    }
}

static void apply_raw_filter(uint8_t voice)
{
    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
    if (voice >= AMY_MUSICAL_VOICE_COUNT ||
        (runtime->backend != AMY_SOUND_BACKEND_RAW && runtime->backend != AMY_SOUND_BACKEND_PCM)) {
        return;
    }

    amy_event event = amy_default_event();
    amy_filter_controls_t controls = current_filter_controls();
    amy_event_apply_filter(&event, &runtime->raw, &controls);
    amy_event_send_raw_control(&event, main_osc_for_voice(voice),
                               pair_osc_for_voice(voice), s_voices[voice].pair_active);
}

static void apply_raw_lfo(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    amy_event event = amy_default_event();
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    amy_event_send_raw_control(&event, main_osc_for_voice(voice),
                               pair_osc_for_voice(voice), s_voices[voice].pair_active);
}

static void apply_raw_envelope(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }

    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
    synth_amy_envelope_t profile_envelope = {0};
    const synth_amy_envelope_t *envelope = effective_raw_envelope(runtime, &profile_envelope);

    amy_event event = amy_default_event();
    amy_event_apply_envelope(&event, envelope);
    amy_event_send_raw_control(&event, main_osc_for_voice(voice),
                               pair_osc_for_voice(voice), s_voices[voice].pair_active);
}

static void apply_raw_portamento(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    amy_event event = amy_default_event();
    event.portamento_ms = s_portamento_ms;
    amy_event_send_raw_control(&event, main_osc_for_voice(voice),
                               pair_osc_for_voice(voice), s_voices[voice].pair_active);
}

static void apply_raw_tremolo(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT) {
        return;
    }
    amy_event event = amy_default_event();
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    amy_event_send_raw_control(&event, main_osc_for_voice(voice),
                               pair_osc_for_voice(voice), s_voices[voice].pair_active);
}

static void apply_raw_stereo_state(uint8_t voice, const amy_sound_runtime_t *runtime)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || runtime == NULL ||
        (runtime->backend != AMY_SOUND_BACKEND_RAW && runtime->backend != AMY_SOUND_BACKEND_PCM) ||
        !s_voices[voice].active) {
        return;
    }

    bool pairs_enabled = amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled);
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

static bool patch_voice_base_osc(uint16_t voice, uint16_t *base_osc)
{
    if (osc_to_voice == NULL || base_osc == NULL) {
        return false;
    }

    for (uint16_t osc = 0; osc < AMY_OSCS; ++osc) {
        if (osc_to_voice[osc] == voice) {
            *base_osc = osc;
            return true;
        }
    }
    return false;
}

static void reserve_drum_oscillators(void)
{
    if (osc_to_voice == NULL) {
        return;
    }

    for (uint8_t i = 0; i < SYNTH_AMY_DRUM_OSC_COUNT; ++i) {
        osc_to_voice[s_drum_osc_base + i] = AMY_DRUM_OSC_RESERVATION_OWNER;
    }
}

static void apply_patch_pan_to_voice(uint16_t voice, float pan)
{
    uint16_t base_osc = 0;
    if (!patch_voice_base_osc(voice, &base_osc)) {
        return;
    }

    for (uint8_t osc = 0; osc < s_patch_oscs_per_voice; ++osc) {
        uint16_t target_osc = (uint16_t)(base_osc + osc);
        if (target_osc >= AMY_OSCS) {
            break;
        }

        amy_event event = amy_default_event();
        event.osc = target_osc;
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

static bool patch_filter_override_active(void)
{
    return s_filter_enabled && s_filter_cutoff_hz < (FILTER_CUTOFF_MAX_HZ - 1.0f);
}

static void apply_patch_filter(void)
{
    if (!s_patch_loaded) {
        return;
    }

    amy_event event = amy_default_event();
    event.filter_type = FILTER_LPF;
    event.filter_freq_coefs[COEF_CONST] = s_filter_cutoff_hz;
    event.resonance = 0.55f;
    amy_event_send_patch_control(&event, s_patch_oscs_per_voice, s_patch_pair_loaded);
}

static void apply_patch_lfo(void)
{
    if (!s_patch_loaded) {
        return;
    }

    amy_event event = amy_default_event();
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    amy_event_send_patch_control(&event, s_patch_oscs_per_voice, s_patch_pair_loaded);
}

static void apply_patch_tremolo(void)
{
    if (!s_patch_loaded) {
        return;
    }

    amy_event event = amy_default_event();
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    amy_event_send_patch_control(&event, s_patch_oscs_per_voice, s_patch_pair_loaded);
}

static void apply_patch_envelope(void)
{
    if (!s_patch_loaded) {
        return;
    }

    amy_event event = amy_default_event();
    amy_event_apply_envelope(&event, &s_envelope);
    amy_event_send_patch_control(&event, s_patch_oscs_per_voice, s_patch_pair_loaded);
}

static void apply_patch_portamento(void)
{
    if (!s_patch_loaded) {
        return;
    }

    amy_event event = amy_default_event();
    event.portamento_ms = s_portamento_ms;
    amy_event_send_patch_control(&event, s_patch_oscs_per_voice, s_patch_pair_loaded);
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

static void configure_patch_synth_instance(uint8_t synth_id,
                                           const amy_sound_runtime_t *runtime,
                                           float level_gain)
{
    if (runtime == NULL || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    amy_event event = amy_default_event();
    event.synth = synth_id;
    event.patch_number = runtime->patch_number;
    event.num_voices = amy_runtime_voice_limit(runtime, s_voice_limit);
    event.synth_level = level_gain;
    amy_add_event(&event);
}

static void configure_patch_synth_with_gain(const amy_sound_runtime_t *runtime, float level_gain)
{
    if (!s_initialized || runtime == NULL || runtime->backend != AMY_SOUND_BACKEND_PATCH) {
        return;
    }

    uint8_t voices = amy_runtime_voice_limit(runtime, s_voice_limit);
    configure_patch_synth_instance(AMY_PATCH_MAIN_SYNTH, runtime, level_gain);

    s_patch_loaded = true;
    s_patch_pair_loaded = false;
    s_patch_number = runtime->patch_number;
    s_patch_voice_count = voices;
    s_patch_oscs_per_voice = runtime->oscs_per_voice;
    s_patch_level_gain = level_gain;

    if (amy_runtime_supports_stereo_pairs(runtime, s_stereo_enabled)) {
        configure_patch_synth_instance(AMY_PATCH_PAIR_SYNTH, runtime, level_gain);
        s_patch_pair_loaded = true;
    }

    apply_patch_pan();
    apply_patch_runtime_controls(false);
    ESP_LOGI(TAG,
             "patch synths=%u patch=%u %s voices=%u oscs_per_voice=%u level_gain=%.4f alloc_oscs=%u",
             (unsigned)(1U + (s_patch_pair_loaded ? 1U : 0U)),
             runtime->patch_number,
             runtime->patch_label != NULL ? runtime->patch_label : "",
             voices,
             runtime->oscs_per_voice,
             level_gain,
             (unsigned)allocated_patch_osc_count());
}

static void configure_patch_synth(const amy_sound_runtime_t *runtime)
{
    configure_patch_synth_with_gain(runtime, synth_level_calibration_sound_gain(s_sound));
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
    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
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
    s_patch_level_gain = 1.0f;
}

esp_err_t synth_amy_set_sound(synth_sound_t sound)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sound == SYNTH_SOUND_MIC_SAMPLE && !s_mic_sample_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(sound);
    if (s_initialized && sound == s_sound &&
        (runtime->backend != AMY_SOUND_BACKEND_PATCH || s_patch_loaded)) {
        return ESP_OK;
    }

    if (s_initialized) {
        amy_graph_lock();
    }

    if (s_initialized) {
        esp_err_t err = synth_amy_musical_all_off();
        if (err != ESP_OK) {
            amy_graph_unlock();
            ESP_LOGE(TAG, "musical all off failed: %s", esp_err_to_name(err));
            return err;
        }
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
             amy_runtime_backend_name(runtime->backend),
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
    return amy_runtime_for_sound(sound)->velocity;
}

bool synth_amy_sound_patch_info(synth_sound_t sound, uint16_t *patch_number, const char **patch_label)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return false;
    }

    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(sound);
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
    configure_patch_synth_with_gain(&runtime, 1.0f);
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

esp_err_t synth_amy_drum_hit_with_gain(uint8_t pcm_preset,
                                      uint8_t midi_note,
                                      uint8_t velocity,
                                      float level_gain)
{
    if (!s_initialized || SYNTH_AMY_DRUM_OSC_COUNT == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm_preset >= pcm_samples || !isfinite(level_gain) || level_gain < 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t osc = (uint16_t)(s_drum_osc_base + s_drum_next_osc);
    s_drum_next_osc = (uint8_t)((s_drum_next_osc + 1U) % SYNTH_AMY_DRUM_OSC_COUNT);

    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = PCM;
    event.preset = pcm_preset;
    event.midi_note = midi_note;
    event.velocity = clamp_float((float)velocity / 127.0f, 0.0f, 1.0f);
    event.amp_coefs[COEF_CONST] = SYNTH_AMY_DRUM_AMP * level_gain;
    event.feedback = 0.0f;
    event.filter_type = FILTER_NONE;
    event.pan_coefs[COEF_CONST] = 0.5f;
    amy_add_event(&event);
    return ESP_OK;
}

esp_err_t synth_amy_install_mic_sample(const int16_t *samples,
                                       uint32_t frames,
                                       float reference_midi_note)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL || frames == 0 || !isfinite(reference_midi_note)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t destination = s_mic_sample_ready
                                     ? (s_mic_sample_preset == 1024U ? 1025U : 1024U)
                                     : 1024U;
    amy_graph_lock();
    int16_t *amy_samples = pcm_load(destination,
                                    frames,
                                    AMY_SAMPLE_RATE,
                                    1,
                                    reference_midi_note,
                                    0,
                                    frames - 1U);
    if (amy_samples == NULL) {
        amy_graph_unlock();
        return ESP_ERR_NO_MEM;
    }
    memcpy(amy_samples, samples, (size_t)frames * sizeof(*samples));
    s_mic_sample_preset = destination;
    s_mic_sample_ready = true;
    amy_graph_unlock();

    ESP_LOGI(TAG,
             "mic sample installed preset=%u frames=%" PRIu32 " reference_note=%.2f",
             (unsigned)destination,
             frames,
             reference_midi_note);
    return ESP_OK;
}

bool synth_amy_mic_sample_ready(void)
{
    return s_mic_sample_ready;
}

esp_err_t synth_amy_drum_hit(uint8_t pcm_preset, uint8_t midi_note, uint8_t velocity)
{
    return synth_amy_drum_hit_with_gain(pcm_preset, midi_note, velocity, 1.0f);
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
    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH) {
        if (s_patch_loaded && s_stereo_enabled && !s_patch_pair_loaded) {
            configure_patch_synth_instance(AMY_PATCH_PAIR_SYNTH, runtime, s_patch_level_gain);
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
             amy_runtime_backend_name(runtime->backend),
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
    esp_err_t err = synth_amy_musical_all_off();
    if (err == ESP_OK && s_patch_loaded) {
        configure_patch_synth(amy_runtime_for_sound(s_sound));
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

static int16_t *render_audio_block(void)
{
    amy_graph_lock();
    int16_t *block = amy_update();
    amy_graph_unlock();
    return block;
}

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
static void capture_audio_input(const int16_t *interleaved, uint32_t frames, uint8_t channels)
{
    synth_mic_sample_process_input(interleaved,
                                   frames,
                                   channels,
                                   ESP_POCKET_AUDIO_MIC_INPUT_CHANNEL);
}
#endif

esp_err_t synth_amy_init(const esp_pocket_audio_hw_t *hw)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

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
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    config.ram_caps_sample = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
    s_drum_osc_base = (uint16_t)(config.max_oscs - SYNTH_AMY_DRUM_OSC_COUNT);
    s_drum_next_osc = 0;
    amy_start(config);
    reserve_drum_oscillators();
    enqueue_master_volume((float)ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT / 100.0f);
    synth_amy_perf_reset();
    ESP_RETURN_ON_ERROR(synth_looper_init(AMY_SAMPLE_RATE, AMY_NCHANS),
                        TAG,
                        "looper init failed");
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    ESP_RETURN_ON_ERROR(synth_mic_sample_init(AMY_SAMPLE_RATE, ESP_POCKET_AUDIO_MIC_MAX_SECONDS),
                        TAG,
                        "mic sampler init failed");
#endif
    s_amy_graph_lock = xSemaphoreCreateMutex();
    if (s_amy_graph_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const synth_amy_audio_config_t audio_config = {
        .hw = hw,
        .render = render_audio_block,
        .active_osc_count = active_osc_count,
        .allocated_patch_osc_count = allocated_patch_osc_count,
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        .input = capture_audio_input,
        .capture_mutes_output = synth_mic_sample_is_recording,
#endif
    };
    ESP_RETURN_ON_ERROR(synth_amy_audio_start(&audio_config), TAG, "audio task start failed");

    s_initialized = true;
    ESP_LOGI(TAG,
             "AMY synth started: %d Hz, %d channels, block=%d (%u us), max_oscs=%u synth_voices=%u drum_oscs=%u juno=%u dx7=%u pcm=%u stereo_pairs=%s multicore=%s perf_log=%s",
             AMY_SAMPLE_RATE,
             AMY_NCHANS,
             AMY_BLOCK_SIZE,
             synth_amy_audio_block_us(),
             (unsigned)config.max_oscs,
             (unsigned)config.max_voices,
             (unsigned)SYNTH_AMY_DRUM_OSC_COUNT,
             (unsigned)ESP_POCKET_AUDIO_AMY_JUNO_VOICES,
             (unsigned)ESP_POCKET_AUDIO_AMY_DX7_VOICES,
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

esp_err_t synth_amy_reset_exclusive(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_graph_lock();
    amy_deltas_reset();
    amy_reset_oscs();
    reserve_drum_oscillators();

    memset(s_voices, 0, sizeof(s_voices));
    s_voice_age = 0;
    s_voice_limit = AMY_MUSICAL_VOICE_COUNT;
    s_envelope_override_enabled = false;
    s_portamento_ms = 0;
    s_sound = SYNTH_SOUND_SAW;
    s_patch_loaded = false;
    s_patch_pair_loaded = false;
    s_patch_number = 0;
    s_patch_voice_count = 0;
    s_patch_oscs_per_voice = 0;
    s_patch_level_gain = 1.0f;
    s_drum_next_osc = 0;
    s_filter_enabled = true;
    s_filter_cutoff_hz = FILTER_CUTOFF_MAX_HZ;
    s_lfo_enabled = false;
    s_lfo_pitch_octaves = 0.0f;
    s_tremolo_enabled = false;
    s_tremolo_amp_offset = 0.0f;
    s_stereo_enabled = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0;
    enqueue_master_volume((float)ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT / 100.0f);
    amy_graph_unlock();
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

    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH && !s_patch_loaded) {
        configure_patch_synth(runtime);
    }

    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT];
    amy_voice_map_build_assignment(chord, assignments);

    const uint8_t *priority = amy_voice_map_role_priority(runtime->backend);
    uint8_t limit = amy_runtime_voice_limit(runtime, s_voice_limit);
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
             amy_runtime_backend_name(runtime->backend),
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

    const amy_sound_runtime_t *runtime = amy_runtime_for_sound(s_sound);
    if (runtime->backend == AMY_SOUND_BACKEND_PATCH && !s_patch_loaded) {
        configure_patch_synth(runtime);
    }

    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT];
    amy_voice_map_build_assignment(chord, assignments);

    const uint8_t *priority = amy_voice_map_role_priority(runtime->backend);
    uint8_t limit = amy_runtime_voice_limit(runtime, s_voice_limit);
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

esp_err_t synth_amy_musical_all_off(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        release_voice(i);
    }
    return ESP_OK;
}

esp_err_t synth_amy_all_off(void)
{
    esp_err_t err = synth_amy_musical_all_off();
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < SYNTH_AMY_DRUM_OSC_COUNT; ++i) {
        amy_note_off_raw_osc((uint16_t)(s_drum_osc_base + i));
    }
    return ESP_OK;
}
