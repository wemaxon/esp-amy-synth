#include "synth/amy_audio.h"

#include <stddef.h>

#include "amy.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "synth/amy_flanger.h"
#include "synth/amy_perf.h"
#include "synth/looper.h"

#define SYNTH_AUDIO_BLOCK_US (((uint32_t)AMY_BLOCK_SIZE * 1000000U) / (uint32_t)AMY_SAMPLE_RATE)

static const char *TAG = "SYNTH_AMY";

static const uint32_t AUDIO_TASK_STACK_SIZE = 8192;
static const UBaseType_t AUDIO_TASK_PRIORITY = configMAX_PRIORITIES - 1;
static const BaseType_t AUDIO_TASK_CORE = 1;

static synth_amy_audio_config_t s_config;
static TaskHandle_t s_audio_task;

static void audio_task(void *arg)
{
    (void)arg;

    while (true) {
        int64_t render_start_us = esp_timer_get_time();
        int16_t *block = s_config.render();
        int64_t amy_done_us = esp_timer_get_time();
        synth_amy_flanger_process(block);
        int64_t flanger_done_us = esp_timer_get_time();
        synth_looper_process_audio(block, AMY_BLOCK_SIZE, AMY_NCHANS);
        int64_t looper_done_us = esp_timer_get_time();
        int64_t render_done_us = esp_timer_get_time();
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_config.hw->i2s_tx,
            block,
            AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
        int64_t write_done_us = esp_timer_get_time();
        synth_amy_perf_record_block((uint32_t)(render_done_us - render_start_us),
                                    (uint32_t)(amy_done_us - render_start_us),
                                    (uint32_t)(flanger_done_us - amy_done_us),
                                    (uint32_t)(looper_done_us - flanger_done_us),
                                    (uint32_t)(write_done_us - render_done_us),
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
        config->render == NULL ||
        config->active_osc_count == NULL ||
        config->allocated_patch_osc_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audio_task,
        "amy_audio",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AUDIO_TASK_PRIORITY,
        &s_audio_task,
        AUDIO_TASK_CORE
    );
    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
