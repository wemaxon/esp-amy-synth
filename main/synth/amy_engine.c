#include "synth/amy_engine.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
#define ESP_POCKET_AUDIO_AMY_STEREO_PAIRS 0
#endif

#define AMY_MUSICAL_VOICE_COUNT 6
#define SYNTH_DELAY_MAX_SAMPLES 32768U
#define SYNTH_DELAY_MAX_MS      (((float)SYNTH_DELAY_MAX_SAMPLES * 1000.0f) / (float)AMY_SAMPLE_RATE)
#define SYNTH_FLANGER_BUFFER_LEN 512U
#define SYNTH_FLANGER_BUFFER_MASK (SYNTH_FLANGER_BUFFER_LEN - 1U)
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
#define AMY_STEREO_PAIR_OFFSET  AMY_MUSICAL_VOICE_COUNT
#define AMY_OSC_COUNT           (AMY_MUSICAL_VOICE_COUNT * 2)
#else
#define AMY_OSC_COUNT           AMY_MUSICAL_VOICE_COUNT
#endif

typedef struct {
    bool active;
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
    bool pair_active;
#endif
    uint8_t slot;
    uint8_t note;
    uint32_t age;
} amy_voice_t;

static const char *TAG = "SYNTH_AMY";

#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
static const float NOTE_VELOCITY = 0.40f;
static const float NOTE_AMP = 0.65f;
static const float STEREO_PAIR_DETUNE_SEMITONES = 0.04f;
#else
static const float NOTE_VELOCITY = 0.55f;
static const float NOTE_AMP = 0.9f;
#endif
static const float FILTER_CUTOFF_MIN_HZ = 20.0f;
static const float FILTER_CUTOFF_MAX_HZ = 20000.0f;
static const float SYNTH_REVERB_LEVEL = 0.35f;
static const float SYNTH_REVERB_LIVENESS = 0.85f;
static const float SYNTH_REVERB_DAMPING = 0.45f;
static const float SYNTH_REVERB_XOVER_HZ = 3000.0f;
static const float SYNTH_CHORUS_LEVEL = 0.45f;
static const float SYNTH_CHORUS_LFO_HZ = 0.55f;
static const float SYNTH_CHORUS_DEPTH = 0.55f;
static const float SYNTH_CHORUS_MAX_DELAY_SAMPLES = 320.0f;
static const float SYNTH_DELAY_LEVEL = 0.38f;
static const float SYNTH_DELAY_FEEDBACK = 0.32f;
static const float SYNTH_DELAY_FILTER_COEF = 0.28f;
static const float SYNTH_TREMOLO_MAX_DEPTH = 0.95f;
static const float SYNTH_FLANGER_RATE_HZ = 0.35f;
static const float SYNTH_FLANGER_MIN_DELAY_MS = 0.70f;
static const float SYNTH_FLANGER_MAX_DELAY_MS = 6.00f;
static const float SYNTH_FLANGER_FEEDBACK = 0.45f;
static const float SYNTH_FLANGER_DRY = 0.80f;
static const float SYNTH_FLANGER_WET = 0.50f;
static const float LFO_MAX_PITCH_SEMITONES = 2.0f;
static const float TWO_PI = 6.283185307f;
static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const UBaseType_t AUDIO_TASK_PRIORITY = 7;
static const BaseType_t AUDIO_TASK_CORE = 1;

#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
/* Six musical voices, each with oscillator voice+6 panned opposite and slightly
 * detuned for width.
 */
static const float VOICE_MAIN_PANS[AMY_MUSICAL_VOICE_COUNT] = {
    0.20f, 0.80f, 0.35f, 0.65f, 0.45f, 0.55f,
};
#endif

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_audio_task;
static bool s_initialized;
static amy_voice_t s_voices[AMY_MUSICAL_VOICE_COUNT];
static uint32_t s_voice_age;
static synth_sound_t s_sound = SYNTH_SOUND_SAW;
static bool s_filter_enabled = true;
static float s_filter_cutoff_hz = 4200.0f;
static bool s_lfo_enabled;
static volatile float s_lfo_pitch_octaves;
static bool s_tremolo_enabled;
static volatile float s_tremolo_amp_offset;
static bool s_flanger_enabled;
static float s_flanger_buffer[AMY_NCHANS][SYNTH_FLANGER_BUFFER_LEN];
static uint16_t s_flanger_write_index;
static float s_flanger_phase;

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

static uint8_t amy_wave_for_sound(synth_sound_t sound)
{
    switch (sound) {
    case SYNTH_SOUND_SINE:
    case SYNTH_SOUND_FM_EPIANO:
        return SINE;
    case SYNTH_SOUND_TRIANGLE:
    case SYNTH_SOUND_FM_BELL:
    case SYNTH_SOUND_CLARINET:
        return TRIANGLE;
    case SYNTH_SOUND_SQUARE:
        return PULSE;
    case SYNTH_SOUND_SAW:
    case SYNTH_SOUND_FM_HX7:
    case SYNTH_SOUND_STRINGS:
    case SYNTH_SOUND_PIANO:
    case SYNTH_SOUND_JUNO_POLY:
    default:
        return SAW_UP;
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

static uint8_t main_osc_for_voice(uint8_t voice)
{
    return voice;
}

#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
static uint8_t pair_osc_for_voice(uint8_t voice)
{
    return (uint8_t)(voice + AMY_STEREO_PAIR_OFFSET);
}

static float main_pan_for_voice(uint8_t voice)
{
    return voice < AMY_MUSICAL_VOICE_COUNT ? VOICE_MAIN_PANS[voice] : 0.5f;
}

static float pair_pan_for_voice(uint8_t voice)
{
    return 1.0f - main_pan_for_voice(voice);
}
#endif

static void amy_apply_osc_filter(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.filter_type = s_filter_enabled ? FILTER_LPF : FILTER_NONE;
    if (s_filter_enabled) {
        event.filter_freq_coefs[COEF_CONST] = s_filter_cutoff_hz;
        event.resonance = 0.6f;
    }
    amy_add_event(&event);
}

static void amy_apply_osc_lfo(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    amy_add_event(&event);
}

static void amy_apply_osc_tremolo(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    amy_add_event(&event);
}

static void amy_note_on(uint8_t osc, uint8_t midi_note, float pan, float detune_semitones)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = amy_wave_for_sound(s_sound);
    event.midi_note = (float)midi_note + detune_semitones;
    event.velocity = NOTE_VELOCITY;
    event.amp_coefs[COEF_CONST] = NOTE_AMP;
    event.amp_coefs[COEF_EXT1] = s_tremolo_enabled ? 1.0f : 0.0f;
    event.freq_coefs[COEF_EXT0] = s_lfo_enabled ? 1.0f : 0.0f;
    event.pan_coefs[COEF_CONST] = clamp_float(pan, 0.0f, 1.0f);
    event.filter_type = s_filter_enabled ? FILTER_LPF : FILTER_NONE;
    if (s_filter_enabled) {
        event.filter_freq_coefs[COEF_CONST] = s_filter_cutoff_hz;
        event.resonance = 0.6f;
    }
    event.eg_type[0] = ENVELOPE_NORMAL;
    event.eg0_times[0] = 8;
    event.eg0_values[0] = 1.0f;
    event.eg0_times[1] = 550;
    event.eg0_values[1] = 0.68f;
    event.eg0_times[2] = 900;
    event.eg0_values[2] = 0.0f;
    amy_add_event(&event);
}

static void amy_note_off(uint8_t osc);

esp_err_t synth_amy_set_sound(synth_sound_t sound)
{
    if (sound >= SYNTH_SOUND_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_sound = sound;
    ESP_LOGI(TAG, "sound=%s engine=%s", synth_sound_name(sound), synth_sound_engine_name(sound));
    return ESP_OK;
}

esp_err_t synth_amy_set_volume(float normalized_volume)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    normalized_volume = clamp_float(normalized_volume, 0.0f, 1.0f);
    event.volume = normalized_volume * 10.0f;
    amy_add_event(&event);
    return ESP_OK;
}

esp_err_t synth_amy_set_filter_enabled(bool enabled)
{
    s_filter_enabled = enabled;
    if (!s_initialized) {
        return ESP_OK;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            continue;
        }
        amy_apply_osc_filter(main_osc_for_voice(i));
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
        if (s_voices[i].pair_active) {
            amy_apply_osc_filter(pair_osc_for_voice(i));
        }
#endif
    }
    return ESP_OK;
}

esp_err_t synth_amy_set_filter_cutoff(float cutoff_hz)
{
    s_filter_cutoff_hz = clamp_float(cutoff_hz, FILTER_CUTOFF_MIN_HZ, FILTER_CUTOFF_MAX_HZ);
    if (!s_initialized) {
        return ESP_OK;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            continue;
        }
        amy_apply_osc_filter(main_osc_for_voice(i));
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
        if (s_voices[i].pair_active) {
            amy_apply_osc_filter(pair_osc_for_voice(i));
        }
#endif
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

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            continue;
        }
        amy_apply_osc_tremolo(main_osc_for_voice(i));
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
        if (s_voices[i].pair_active) {
            amy_apply_osc_tremolo(pair_osc_for_voice(i));
        }
#endif
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
    if (enabled != s_flanger_enabled) {
        memset(s_flanger_buffer, 0, sizeof(s_flanger_buffer));
        s_flanger_write_index = 0;
        s_flanger_phase = 0.0f;
    }

    s_flanger_enabled = enabled;
    ESP_LOGI(TAG, "flanger %s", enabled ? "on" : "off");
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

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            continue;
        }
        amy_apply_osc_lfo(main_osc_for_voice(i));
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
        if (s_voices[i].pair_active) {
            amy_apply_osc_lfo(pair_osc_for_voice(i));
        }
#endif
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

static int16_t clamp_int16(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)value;
}

static float flanger_delay_sample(uint8_t channel, float delay_samples)
{
    uint16_t base_delay = (uint16_t)delay_samples;
    float frac = delay_samples - (float)base_delay;
    uint16_t index_a = (uint16_t)((s_flanger_write_index + SYNTH_FLANGER_BUFFER_LEN - base_delay) &
                                  SYNTH_FLANGER_BUFFER_MASK);
    uint16_t index_b = (uint16_t)((index_a + SYNTH_FLANGER_BUFFER_LEN - 1U) &
                                  SYNTH_FLANGER_BUFFER_MASK);
    float a = s_flanger_buffer[channel][index_a];
    float b = s_flanger_buffer[channel][index_b];
    return a + (b - a) * frac;
}

static void apply_flanger(int16_t *block)
{
    if (!s_flanger_enabled || block == NULL) {
        return;
    }

    float min_delay = (SYNTH_FLANGER_MIN_DELAY_MS * (float)AMY_SAMPLE_RATE) / 1000.0f;
    float max_delay = (SYNTH_FLANGER_MAX_DELAY_MS * (float)AMY_SAMPLE_RATE) / 1000.0f;
    float delay_range = max_delay - min_delay;
    float phase_increment = SYNTH_FLANGER_RATE_HZ / (float)AMY_SAMPLE_RATE;

    for (uint16_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        for (uint8_t channel = 0; channel < AMY_NCHANS; ++channel) {
            float channel_phase = s_flanger_phase;
            if (AMY_NCHANS > 1 && channel == 1) {
                channel_phase += 0.25f;
                if (channel_phase >= 1.0f) {
                    channel_phase -= 1.0f;
                }
            }

            float lfo = 0.5f + 0.5f * sinf(TWO_PI * channel_phase);
            float delayed = flanger_delay_sample(channel, min_delay + delay_range * lfo);
            uint16_t sample_index = (uint16_t)(i * AMY_NCHANS + channel);
            float input = (float)block[sample_index] / 32768.0f;
            float feedback_sample = clamp_float(input + delayed * SYNTH_FLANGER_FEEDBACK, -1.0f, 1.0f);
            float output = input * SYNTH_FLANGER_DRY + delayed * SYNTH_FLANGER_WET;

            s_flanger_buffer[channel][s_flanger_write_index] = feedback_sample;
            block[sample_index] = clamp_int16(output * 32767.0f);
        }

        s_flanger_phase += phase_increment;
        if (s_flanger_phase >= 1.0f) {
            s_flanger_phase -= 1.0f;
        }
        s_flanger_write_index = (uint16_t)((s_flanger_write_index + 1U) & SYNTH_FLANGER_BUFFER_MASK);
    }
}

static void amy_note_off(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.velocity = 0.0f;
    amy_add_event(&event);
}

static void release_voice(uint8_t voice)
{
    if (voice >= AMY_MUSICAL_VOICE_COUNT || !s_voices[voice].active) {
        return;
    }

    amy_note_off(main_osc_for_voice(voice));
#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
    if (s_voices[voice].pair_active) {
        amy_note_off(pair_osc_for_voice(voice));
    }
    s_voices[voice].pair_active = false;
#endif
    s_voices[voice].active = false;
}

static uint8_t choose_voice(void)
{
    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            return i;
        }
    }

    uint8_t oldest_voice = 0;
    uint32_t oldest_age = s_voices[0].age;
    for (uint8_t i = 1; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].age < oldest_age) {
            oldest_age = s_voices[i].age;
            oldest_voice = i;
        }
    }
    release_voice(oldest_voice);
    return oldest_voice;
}

static void audio_task(void *arg)
{
    (void)arg;

    while (true) {
        int16_t *block = amy_update();
        apply_flanger(block);
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_hw->i2s_tx,
            block,
            AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
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
    config.platform.multicore = 0;
    config.platform.multithread = 0;
    config.max_oscs = AMY_OSC_COUNT;
    config.max_voices = AMY_OSC_COUNT;
    config.max_synths = 1;
    config.max_memory_patches = 4;
    config.max_sequencer_tags = 8;
    config.amy_external_coef_hook = amy_external_coef;
    amy_start(config);
    amy_global.echo.max_delay_samples = SYNTH_DELAY_MAX_SAMPLES;

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
    ESP_LOGI(TAG, "AMY synth started: %d Hz, %d channels, block=%d, voices=%u max_oscs=%u stereo_pairs=%s",
             AMY_SAMPLE_RATE,
             AMY_NCHANS,
             AMY_BLOCK_SIZE,
             AMY_MUSICAL_VOICE_COUNT,
             AMY_OSC_COUNT,
             ESP_POCKET_AUDIO_AMY_STEREO_PAIRS ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_amy_chord_off(uint8_t slot)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < AMY_MUSICAL_VOICE_COUNT; ++i) {
        if (s_voices[i].active && s_voices[i].slot == slot) {
            release_voice(i);
        }
    }
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

    ESP_RETURN_ON_ERROR(synth_amy_chord_off(slot), TAG, "chord off failed");

    uint8_t note_count = chord->note_count > AMY_MUSICAL_VOICE_COUNT ?
        AMY_MUSICAL_VOICE_COUNT : chord->note_count;
    for (uint8_t i = 0; i < note_count; ++i) {
        uint8_t voice = choose_voice();
        s_voices[voice].active = true;
        s_voices[voice].slot = slot;
        s_voices[voice].note = chord->notes[i];
        s_voices[voice].age = ++s_voice_age;

#if ESP_POCKET_AUDIO_AMY_STEREO_PAIRS
        s_voices[voice].pair_active = false;
        amy_note_on(main_osc_for_voice(voice), chord->notes[i], main_pan_for_voice(voice), 0.0f);
        amy_note_on(pair_osc_for_voice(voice), chord->notes[i], pair_pan_for_voice(voice), STEREO_PAIR_DETUNE_SEMITONES);
        s_voices[voice].pair_active = true;
#else
        amy_note_on(main_osc_for_voice(voice), chord->notes[i], 0.5f, 0.0f);
#endif
    }

    return ESP_OK;
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
