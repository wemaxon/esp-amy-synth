#include "synth/amy_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AMY_VOICE_COUNT 8

typedef struct {
    bool active;
    uint8_t slot;
    uint8_t note;
    uint32_t age;
} amy_voice_t;

static const char *TAG = "SYNTH_AMY";

static const float NOTE_VELOCITY = 0.55f;
static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const UBaseType_t AUDIO_TASK_PRIORITY = 7;
static const BaseType_t AUDIO_TASK_CORE = 1;

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_audio_task;
static bool s_initialized;
static amy_voice_t s_voices[AMY_VOICE_COUNT];
static uint32_t s_voice_age;

static void amy_note_on(uint8_t osc, uint8_t midi_note)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = TRIANGLE;
    event.midi_note = midi_note;
    event.velocity = NOTE_VELOCITY;
    event.amp_coefs[COEF_CONST] = 0.9f;
    event.filter_type = FILTER_LPF;
    event.filter_freq_coefs[COEF_CONST] = 4200.0f;
    event.resonance = 0.6f;
    event.eg_type[0] = ENVELOPE_NORMAL;
    event.eg0_times[0] = 8;
    event.eg0_values[0] = 1.0f;
    event.eg0_times[1] = 550;
    event.eg0_values[1] = 0.68f;
    event.eg0_times[2] = 900;
    event.eg0_values[2] = 0.0f;
    amy_add_event(&event);
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
    if (voice >= AMY_VOICE_COUNT || !s_voices[voice].active) {
        return;
    }

    amy_note_off(voice);
    s_voices[voice].active = false;
}

static uint8_t choose_voice(void)
{
    for (uint8_t i = 0; i < AMY_VOICE_COUNT; ++i) {
        if (!s_voices[i].active) {
            return i;
        }
    }

    uint8_t oldest_voice = 0;
    uint32_t oldest_age = s_voices[0].age;
    for (uint8_t i = 1; i < AMY_VOICE_COUNT; ++i) {
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
    config.features.reverb = 0;
    config.features.chorus = 0;
    config.features.echo = 0;
    config.platform.multicore = 0;
    config.platform.multithread = 0;
    config.max_oscs = AMY_VOICE_COUNT;
    config.max_voices = AMY_VOICE_COUNT;
    config.max_synths = 1;
    config.max_memory_patches = 4;
    config.max_sequencer_tags = 8;
    amy_start(config);

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
    ESP_LOGI(TAG, "AMY synth started: %d Hz, %d channels, block=%d, voices=%u",
             AMY_SAMPLE_RATE,
             AMY_NCHANS,
             AMY_BLOCK_SIZE,
             AMY_VOICE_COUNT);
    return ESP_OK;
}

esp_err_t synth_amy_chord_off(uint8_t slot)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < AMY_VOICE_COUNT; ++i) {
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

    for (uint8_t i = 0; i < chord->note_count; ++i) {
        uint8_t voice = choose_voice();
        s_voices[voice].active = true;
        s_voices[voice].slot = slot;
        s_voices[voice].note = chord->notes[i];
        s_voices[voice].age = ++s_voice_age;
        amy_note_on(voice, chord->notes[i]);
    }

    return ESP_OK;
}

esp_err_t synth_amy_all_off(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < AMY_VOICE_COUNT; ++i) {
        release_voice(i);
    }
    return ESP_OK;
}
