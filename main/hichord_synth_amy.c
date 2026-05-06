#include "hichord_synth_amy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HICHORD_SYNTH";

static const uint8_t SYNTH_MAX_VOICES = HICHORD_MAX_CHORD_NOTES;
static const float SYNTH_VELOCITY = 0.55f;
static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const UBaseType_t AUDIO_TASK_PRIORITY = 7;
static const BaseType_t AUDIO_TASK_CORE = 1;

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_audio_task;
static bool s_initialized;
static bool s_voice_active[HICHORD_MAX_CHORD_NOTES];

static void amy_send_osc_note_on(uint8_t osc, uint8_t midi_note)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = TRIANGLE;
    event.midi_note = midi_note;
    event.velocity = SYNTH_VELOCITY;
    event.amp_coefs[COEF_CONST] = 0.9f;
    event.filter_type = FILTER_LPF;
    event.filter_freq_coefs[COEF_CONST] = 4200.0f;
    event.resonance = 0.6f;
    event.eg_type[0] = ENVELOPE_NORMAL;
    event.eg0_times[0] = 8;
    event.eg0_values[0] = 1.0f;
    event.eg0_times[1] = 550;
    event.eg0_values[1] = 0.68f;
    event.eg0_times[2] = 1200;
    event.eg0_values[2] = 0.0f;
    amy_add_event(&event);
}

static void amy_send_osc_note_off(uint8_t osc)
{
    amy_event event = amy_default_event();
    event.osc = osc;
    event.velocity = 0.0f;
    amy_add_event(&event);
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

esp_err_t hichord_synth_amy_init(const esp_pocket_audio_hw_t *hw)
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
    config.max_oscs = 16;
    config.max_voices = SYNTH_MAX_VOICES;
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
             SYNTH_MAX_VOICES);
    return ESP_OK;
}

esp_err_t hichord_synth_amy_release_chord(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (s_voice_active[i]) {
            amy_send_osc_note_off(i);
            s_voice_active[i] = false;
        }
    }
    return ESP_OK;
}

esp_err_t hichord_synth_amy_play_chord(const hichord_chord_t *chord)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (chord == NULL || chord->note_count == 0 || chord->note_count > SYNTH_MAX_VOICES) {
        return ESP_ERR_INVALID_ARG;
    }

    hichord_synth_amy_release_chord();

    for (uint8_t i = 0; i < chord->note_count; ++i) {
        amy_send_osc_note_on(i, chord->notes[i]);
        s_voice_active[i] = true;
    }

    return ESP_OK;
}
