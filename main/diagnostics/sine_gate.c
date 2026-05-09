#include "diagnostics/sine_gate.h"

#include <math.h>
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
#include "hmi/controls.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SINE_TASK_STACK_SIZE 4096
#define SINE_TASK_PRIORITY 7
#define SINE_TASK_CORE 1
#define SINE_FRAMES 256U
#define SINE_CHANNELS 2U
#define SINE_TABLE_SIZE 512U
#define SINE_TABLE_MASK (SINE_TABLE_SIZE - 1U)
#define SINE_LOG_INTERVAL_US 2000000LL
#define SINE_BLOCK_US ((SINE_FRAMES * 1000000U) / ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ)
#define SINE_GATE_LEVEL 0.24f

typedef struct {
    uint32_t blocks;
    uint32_t short_writes;
    uint64_t write_us_total;
    uint32_t write_us_max;
    int64_t next_log_us;
} sine_stats_t;

static const char *TAG = "DIAG_SINE";

static const float CHORD_KEY_FREQ_HZ[] = {
    261.6256f,
    293.6648f,
    329.6276f,
    349.2282f,
    391.9954f,
    440.0000f,
    493.8833f,
};

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_task;
static int16_t s_block[SINE_FRAMES * SINE_CHANNELS];
static float s_sine_table[SINE_TABLE_SIZE];
static bool s_sine_table_ready;

static void init_sine_table(void)
{
    if (s_sine_table_ready) {
        return;
    }

    for (uint16_t i = 0; i < SINE_TABLE_SIZE; ++i) {
        float phase = ((float)i * 2.0f * (float)M_PI) / (float)SINE_TABLE_SIZE;
        s_sine_table[i] = sinf(phase);
    }
    s_sine_table_ready = true;
}

static void draw_status_once(const esp_pocket_audio_hw_t *hw)
{
#if ESP_POCKET_AUDIO_OLED_ENABLED
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    pocket_oled_clear((pocket_oled_t *)&hw->display);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 0, "DIAGNOSTIC");
#if ESP_POCKET_AUDIO_DIAG_SINE_AUTO_GATE
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 2, "SINE AUTO GATE");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 5, "NO KEY NEEDED");
#else
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 2, "SINE GATE");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 5, "HOLD CHORD KEY");
#endif
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 4, "AMY OFF");
    ESP_ERROR_CHECK_WITHOUT_ABORT(oled_display_flush_cooperatively((pocket_oled_t *)&hw->display,
                                                                   pdMS_TO_TICKS(50)));
#else
    (void)hw;
#endif
}

#if !ESP_POCKET_AUDIO_DIAG_SINE_AUTO_GATE
static int8_t first_pressed_chord_key(uint32_t pressed_mask)
{
    for (uint8_t key = 0; key < 7; ++key) {
        if ((pressed_mask & (1UL << key)) != 0) {
            return (int8_t)key;
        }
    }
    return -1;
}
#endif

static int8_t auto_gate_key(void)
{
#if ESP_POCKET_AUDIO_DIAG_SINE_AUTO_GATE
    const uint32_t on_ms = ESP_POCKET_AUDIO_DIAG_SINE_AUTO_ON_MS;
    const uint32_t off_ms = ESP_POCKET_AUDIO_DIAG_SINE_AUTO_OFF_MS;
    const uint32_t period_ms = on_ms + off_ms;
    if (period_ms == 0) {
        return -1;
    }

    uint32_t phase_ms = (uint32_t)((esp_timer_get_time() / 1000LL) % period_ms);
    return phase_ms < on_ms ? 0 : -1;
#else
    return first_pressed_chord_key(controls_pressed_mask());
#endif
}

static int16_t sample_to_i16(float sample)
{
    if (sample > 0.999f) {
        sample = 0.999f;
    } else if (sample < -1.0f) {
        sample = -1.0f;
    }
    return (int16_t)(sample * 32767.0f);
}

static void fill_sine_block(float *phase,
                            float *gate,
                            int8_t active_key,
                            int8_t *current_key)
{
    float target_gate = active_key >= 0 ? 1.0f : 0.0f;
    float fade_ms = target_gate > *gate
                        ? (float)ESP_POCKET_AUDIO_DIAG_SINE_ATTACK_MS
                        : (float)ESP_POCKET_AUDIO_DIAG_SINE_RELEASE_MS;
    float gate_step = 1.0f / ((fade_ms * (float)ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ) / 1000.0f);
    if (gate_step > 1.0f) {
        gate_step = 1.0f;
    }

    if (active_key >= 0 && active_key != *current_key) {
        *phase = 0.0f;
        *current_key = active_key;
    }

    int8_t render_key = *current_key >= 0 ? *current_key : 0;
    float freq_hz = CHORD_KEY_FREQ_HZ[render_key];
    float phase_step = (freq_hz * (float)SINE_TABLE_SIZE) /
                       (float)ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ;

    for (uint16_t frame = 0; frame < SINE_FRAMES; ++frame) {
        if (*gate < target_gate) {
            *gate += gate_step;
            if (*gate > target_gate) {
                *gate = target_gate;
            }
        } else if (*gate > target_gate) {
            *gate -= gate_step;
            if (*gate < target_gate) {
                *gate = target_gate;
            }
        }

        uint16_t index = (uint16_t)*phase & SINE_TABLE_MASK;
        int16_t sample = sample_to_i16(s_sine_table[index] * *gate * SINE_GATE_LEVEL);
        for (uint8_t channel = 0; channel < SINE_CHANNELS; ++channel) {
            s_block[(frame * SINE_CHANNELS) + channel] = sample;
        }

        *phase += phase_step;
        while (*phase >= (float)SINE_TABLE_SIZE) {
            *phase -= (float)SINE_TABLE_SIZE;
        }
    }

    if (active_key < 0 && *gate <= 0.0f) {
        *current_key = -1;
    }
}

static void log_stats(sine_stats_t *stats,
                      int64_t now_us,
                      int8_t active_key,
                      float gate)
{
    if (stats->next_log_us == 0) {
        stats->next_log_us = now_us + SINE_LOG_INTERVAL_US;
    }
    if (now_us < stats->next_log_us || stats->blocks == 0) {
        return;
    }

    uint32_t avg_write_us = (uint32_t)(stats->write_us_total / stats->blocks);
    ESP_LOGI(TAG,
             "sine gate blocks=%u key=%d gate=%.2f write_us avg=%u max=%u block_us=%u short=%u heap=%u internal=%u stack_free_words=%u",
             (unsigned)stats->blocks,
             (int)active_key + 1,
             (double)gate,
             (unsigned)avg_write_us,
             (unsigned)stats->write_us_max,
             (unsigned)SINE_BLOCK_US,
             (unsigned)stats->short_writes,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    stats->blocks = 0;
    stats->short_writes = 0;
    stats->write_us_total = 0;
    stats->write_us_max = 0;
    stats->next_log_us = now_us + SINE_LOG_INTERVAL_US;
}

static void sine_task(void *arg)
{
    (void)arg;

    float phase = 0.0f;
    float gate = 0.0f;
    int8_t current_key = -1;
    int8_t last_logged_key = -2;
    sine_stats_t stats = {0};

    ESP_LOGW(TAG,
             "running direct I2S sine gate: codec/I2S/controls active, AMY and synth app disabled, attack=%u ms release=%u ms auto=%s",
             (unsigned)ESP_POCKET_AUDIO_DIAG_SINE_ATTACK_MS,
             (unsigned)ESP_POCKET_AUDIO_DIAG_SINE_RELEASE_MS,
             ESP_POCKET_AUDIO_DIAG_SINE_AUTO_GATE ? "on" : "off");

    while (true) {
        int8_t active_key = auto_gate_key();
        if (active_key != last_logged_key) {
            if (active_key >= 0) {
                ESP_LOGI(TAG, "sine on key=%d freq=%.2f Hz", (int)active_key + 1, (double)CHORD_KEY_FREQ_HZ[active_key]);
            } else {
                ESP_LOGI(TAG, "sine off");
            }
            last_logged_key = active_key;
        }

        fill_sine_block(&phase, &gate, active_key, &current_key);

        int64_t start_us = esp_timer_get_time();
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_hw->i2s_tx,
            s_block,
            sizeof(s_block),
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
        if (err != ESP_OK || bytes_written != sizeof(s_block)) {
            ++stats.short_writes;
            ESP_LOGW(TAG,
                     "I2S sine write failed: %s bytes=%u expected=%u",
                     esp_err_to_name(err),
                     (unsigned)bytes_written,
                     (unsigned)sizeof(s_block));
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        log_stats(&stats, done_us, active_key, gate);
    }
}

esp_err_t diagnostics_sine_gate_start(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_hw = hw;
    init_sine_table();
    draw_status_once(hw);

    BaseType_t task_created = xTaskCreatePinnedToCore(
        sine_task,
        "diag_sine",
        SINE_TASK_STACK_SIZE,
        NULL,
        SINE_TASK_PRIORITY,
        &s_task,
        SINE_TASK_CORE
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
