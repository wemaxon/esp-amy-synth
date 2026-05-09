#include "diagnostics/digital_silence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"

#define SILENCE_TASK_STACK_SIZE 4096
#define SILENCE_TASK_PRIORITY 7
#define SILENCE_TASK_CORE 1
#define SILENCE_FRAMES 256U
#define SILENCE_CHANNELS 2U
#define SILENCE_LOG_INTERVAL_US 2000000LL
#define SILENCE_BLOCK_US ((SILENCE_FRAMES * 1000000U) / ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ)

typedef struct {
    uint32_t blocks;
    uint32_t short_writes;
    uint64_t write_us_total;
    uint32_t write_us_max;
    int64_t next_log_us;
} silence_stats_t;

static const char *TAG = "DIAG_SILENCE";

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_task;
static int16_t s_silence[SILENCE_FRAMES * SILENCE_CHANNELS];

static void draw_status_once(const esp_pocket_audio_hw_t *hw)
{
#if ESP_POCKET_AUDIO_OLED_ENABLED
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    pocket_oled_clear((pocket_oled_t *)&hw->display);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 0, "DIAGNOSTIC");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 2, "DIGITAL SILENCE");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 4, "AMY OFF");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 5, "CONTROLS ON");
    ESP_ERROR_CHECK_WITHOUT_ABORT(oled_display_flush_cooperatively((pocket_oled_t *)&hw->display,
                                                                   pdMS_TO_TICKS(50)));
#else
    (void)hw;
#endif
}

static void log_stats(silence_stats_t *stats, int64_t now_us)
{
    if (stats->next_log_us == 0) {
        stats->next_log_us = now_us + SILENCE_LOG_INTERVAL_US;
    }
    if (now_us < stats->next_log_us || stats->blocks == 0) {
        return;
    }

    uint32_t avg_write_us = (uint32_t)(stats->write_us_total / stats->blocks);
    ESP_LOGI(TAG,
             "digital silence blocks=%u write_us avg=%u max=%u block_us=%u short=%u heap=%u internal=%u stack_free_words=%u",
             (unsigned)stats->blocks,
             (unsigned)avg_write_us,
             (unsigned)stats->write_us_max,
             (unsigned)SILENCE_BLOCK_US,
             (unsigned)stats->short_writes,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    stats->blocks = 0;
    stats->short_writes = 0;
    stats->write_us_total = 0;
    stats->write_us_max = 0;
    stats->next_log_us = now_us + SILENCE_LOG_INTERVAL_US;
}

static void silence_task(void *arg)
{
    (void)arg;

    silence_stats_t stats = {0};
    ESP_LOGW(TAG,
             "running digital silence: codec/I2S/controls active, AMY and synth app disabled");

    while (true) {
        int64_t start_us = esp_timer_get_time();
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_hw->i2s_tx,
            s_silence,
            sizeof(s_silence),
            &bytes_written,
            portMAX_DELAY
        );
        int64_t done_us = esp_timer_get_time();
        uint32_t write_us = (uint32_t)(done_us - start_us);

        ++stats.blocks;
        stats.write_us_total += write_us;
        if (write_us > stats.write_us_max) {
            stats.write_us_max = write_us;
        }
        if (err != ESP_OK || bytes_written != sizeof(s_silence)) {
            ++stats.short_writes;
            ESP_LOGW(TAG,
                     "I2S silence write failed: %s bytes=%u expected=%u",
                     esp_err_to_name(err),
                     (unsigned)bytes_written,
                     (unsigned)sizeof(s_silence));
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        log_stats(&stats, done_us);
    }
}

esp_err_t diagnostics_digital_silence_start(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_hw = hw;
    draw_status_once(hw);

    BaseType_t task_created = xTaskCreatePinnedToCore(
        silence_task,
        "diag_silence",
        SILENCE_TASK_STACK_SIZE,
        NULL,
        SILENCE_TASK_PRIORITY,
        &s_task,
        SILENCE_TASK_CORE
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
