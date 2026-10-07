#include "synth/amy_audio.h"

#include <stddef.h>
#include <string.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_pocket_audio_board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "synth/amy_flanger.h"
#include "synth/amy_perf.h"
#include "synth/looper.h"

#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
#include "synth/audio_meter.h"
#endif

#if ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ != AMY_SAMPLE_RATE
#error "ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ must match AMY_SAMPLE_RATE"
#endif

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED && AMY_NCHANS != 2
#error "Mic sampling requires stereo I2S blocks"
#endif

#define SYNTH_AUDIO_BLOCK_US (((uint32_t)AMY_BLOCK_SIZE * 1000000U) / (uint32_t)AMY_SAMPLE_RATE)

static const char *TAG = "SYNTH_AMY";

static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const BaseType_t AUDIO_TASK_CORE = 1;

static synth_amy_audio_config_t s_config;
static TaskHandle_t s_audio_task;
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
static int16_t s_input_block[AMY_BLOCK_SIZE * AMY_NCHANS];
#endif

static void audio_task(void *arg)
{
    (void)arg;

    while (true) {
        int64_t render_start_us = esp_timer_get_time();
#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
        synth_audio_meter_begin_block();
#endif
        if (s_config.before_render != NULL) {
            s_config.before_render();
        }
        int16_t *block = s_config.render();
        int64_t amy_done_us = esp_timer_get_time();
        /* The flanger runs inside AMY's render on the synth bus. */
        uint32_t flanger_us = synth_amy_flanger_last_process_us();
#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
        synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_DRY,
                                  block,
                                  AMY_BLOCK_SIZE,
                                  AMY_NCHANS);
#endif
        const int16_t *record_block = s_config.record_source != NULL ? s_config.record_source() : block;
        synth_looper_process_audio(block, record_block, AMY_BLOCK_SIZE, AMY_NCHANS);
        int64_t looper_done_us = esp_timer_get_time();
        synth_looper_record_process_time((uint32_t)(looper_done_us - amy_done_us),
                                         SYNTH_AUDIO_BLOCK_US);
#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
        synth_audio_meter_process(SYNTH_AUDIO_METER_TAP_OUTPUT,
                                  block,
                                  AMY_BLOCK_SIZE,
                                  AMY_NCHANS);
#endif
        int64_t render_done_us = esp_timer_get_time();
        if (s_config.capture_mutes_output != NULL && s_config.capture_mutes_output()) {
            memset(block, 0, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(*block));
        }
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_config.hw->i2s_tx,
            block,
            AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
        uint32_t input_us = 0;
        bool input_error = false;
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        size_t bytes_read = 0;
        int64_t read_start_us = esp_timer_get_time();
        esp_err_t read_err = i2s_channel_read(
            s_config.hw->i2s_rx,
            s_input_block,
            sizeof(s_input_block),
            &bytes_read,
            portMAX_DELAY
        );
        input_us = (uint32_t)(esp_timer_get_time() - read_start_us);
        if (read_err == ESP_OK && bytes_read == sizeof(s_input_block)) {
            s_config.input(s_input_block, AMY_BLOCK_SIZE, AMY_NCHANS);
        } else {
            input_error = true;
        }
#endif
        int64_t write_done_us = esp_timer_get_time();
        synth_amy_perf_record_block((uint32_t)(render_done_us - render_start_us),
                                    (uint32_t)(amy_done_us - render_start_us) - flanger_us,
                                    flanger_us,
                                    (uint32_t)(looper_done_us - amy_done_us),
                                    (uint32_t)(write_done_us - render_done_us),
                                    input_us,
                                    input_error,
                                    write_done_us,
                                    s_config.active_osc_count(),
                                    s_config.allocated_patch_osc_count(),
                                    SYNTH_AUDIO_BLOCK_US);
        if (err != ESP_OK || bytes_written != AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t)) {
            ESP_LOGW(TAG, "AMY I2S write failed: %s bytes=%u",
                     esp_err_to_name(err),
                     (unsigned)bytes_written);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

uint32_t synth_amy_audio_block_us(void)
{
    return SYNTH_AUDIO_BLOCK_US;
}

esp_err_t synth_amy_audio_start(const synth_amy_audio_config_t *config)
{
    if (s_audio_task != NULL) {
        return ESP_OK;
    }
    if (config == NULL ||
        config->hw == NULL ||
        config->hw->i2s_tx == NULL ||
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        config->hw->i2s_rx == NULL ||
        config->input == NULL ||
#endif
        config->render == NULL ||
        config->active_osc_count == NULL ||
        config->allocated_patch_osc_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
    esp_err_t meter_err = synth_audio_meter_init(AMY_SAMPLE_RATE,
                                                 AMY_BLOCK_SIZE,
                                                 SYNTH_AUDIO_BLOCK_US);
    if (meter_err != ESP_OK) {
        return meter_err;
    }
#endif
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audio_task,
        "amy_audio",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AMY_FILL_BUFFER_TASK_PRIORITY,
        &s_audio_task,
        AUDIO_TASK_CORE
    );
    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
