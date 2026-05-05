#include "diagnostics.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_pocket_audio_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pocket_oled.h"

static const char *TAG = "diagnostics";

static const TickType_t INPUT_POLL_TICKS = pdMS_TO_TICKS(20);
static const TickType_t JOYSTICK_LOG_TICKS = pdMS_TO_TICKS(2000);
static const TickType_t SUMMARY_LOG_TICKS = pdMS_TO_TICKS(10000);

static const uint32_t AUDIO_TEST_SAMPLE_RATE_HZ = 48000;
static const uint32_t AUDIO_TEST_TONE_HZ = 440;
static const int16_t AUDIO_TEST_AMPLITUDE = 6000;
static const size_t AUDIO_TEST_FRAMES = 256;

static const char *switch_name(esp_pocket_audio_switch_t sw)
{
    switch (sw) {
    case ESP_POCKET_AUDIO_SWITCH_CHORD_1:
        return "KEY_CHORD_1";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_2:
        return "KEY_CHORD_2";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_3:
        return "KEY_CHORD_3";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_4:
        return "KEY_CHORD_4";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_5:
        return "KEY_CHORD_5";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_6:
        return "KEY_CHORD_6";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_7:
        return "KEY_CHORD_7";
    case ESP_POCKET_AUDIO_SWITCH_JOYSTICK_CLICK:
        return "JOYSTICK_SW";
    case ESP_POCKET_AUDIO_SWITCH_KEY_TEMPO:
        return "KEY_TEMPO";
    case ESP_POCKET_AUDIO_SWITCH_KEY_MODE:
        return "KEY_MODE";
    case ESP_POCKET_AUDIO_SWITCH_KEY_SOUND:
        return "KEY_SOUND";
    default:
        return "UNKNOWN";
    }
}

static void log_input_event(const esp_pocket_audio_input_event_t *event)
{
    ESP_LOGI(
        TAG,
        "%s %s",
        switch_name(event->sw),
        event->type == ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_PRESSED ? "pressed" : "released"
    );
}

static int16_t triangle_sample(uint32_t phase)
{
    uint32_t p = phase >> 16;
    int32_t tri = (p < 32768) ? (int32_t)p : (int32_t)(65535 - p);
    int32_t centered = (tri * 4) - 65535;
    return (int16_t)((centered * AUDIO_TEST_AMPLITUDE) / 65535);
}

static void audio_test_task(void *arg)
{
    const esp_pocket_audio_hw_t *hw = (const esp_pocket_audio_hw_t *)arg;
    int16_t frames[AUDIO_TEST_FRAMES * 2];
    uint32_t phase = 0;
    uint32_t phase_step = (uint32_t)(((uint64_t)AUDIO_TEST_TONE_HZ << 32) / AUDIO_TEST_SAMPLE_RATE_HZ);

    ESP_LOGI(TAG, "audio test tone started: %" PRIu32 " Hz triangle", AUDIO_TEST_TONE_HZ);

    while (true) {
        for (size_t i = 0; i < AUDIO_TEST_FRAMES; ++i) {
            int16_t sample = triangle_sample(phase);
            phase += phase_step;
            frames[i * 2] = sample;
            frames[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            hw->i2s_tx,
            frames,
            sizeof(frames),
            &bytes_written,
            pdMS_TO_TICKS(1000)
        );
        if (err != ESP_OK || bytes_written != sizeof(frames)) {
            ESP_LOGW(TAG, "audio test write failed: %s bytes=%u",
                     esp_err_to_name(err),
                     (unsigned)bytes_written);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t diagnostics_start_audio_test(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        audio_test_task,
        "audio_test",
        4096,
        (void *)hw,
        6,
        NULL,
        1
    );

    return (task_created == pdPASS) ? ESP_OK : ESP_FAIL;
}

static void update_display(const esp_pocket_audio_hw_t *hw, uint32_t pressed_mask, const esp_pocket_audio_joystick_state_t *joystick)
{
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    char line[24];
    pocket_oled_clear((pocket_oled_t *)&hw->display);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 0, "ESP-POCKET AUDIO");
    snprintf(line, sizeof(line), "BTN %08" PRIX32, pressed_mask);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 1, line);

    int joy_x = (int)(joystick->x.value * 100.0f);
    int joy_y = (int)(joystick->y.value * 100.0f);
    snprintf(line, sizeof(line), "JOY X%+04d Y%+04d", joy_x, joy_y);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 2, line);

    snprintf(line, sizeof(line), "RAW %05d %05d", joystick->x.raw, joystick->y.raw);
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 3, line);

    snprintf(line, sizeof(line), "DIR %s", esp_pocket_audio_joystick_direction_name(joystick->direction));
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 4, line);

    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 5, "TONE 440HZ");
    pocket_oled_draw_text_row((pocket_oled_t *)&hw->display, 6,
                              esp_pocket_audio_input_int_is_asserted() ? "PCF INT ASSERT" : "PCF INT IDLE");
    ESP_ERROR_CHECK(pocket_oled_flush((pocket_oled_t *)&hw->display));
}

void diagnostics_run(const esp_pocket_audio_hw_t *hw)
{
    TickType_t next_joystick_log = 0;
    TickType_t next_summary_log = 0;
    uint32_t last_pressed_mask = UINT32_MAX;
    esp_pocket_audio_joystick_state_t joystick = {0};

    while (true) {
        TickType_t now = xTaskGetTickCount();

        esp_pocket_audio_input_event_t event = {0};
        while (esp_pocket_audio_input_pop_event(&event) == ESP_OK) {
            log_input_event(&event);
        }

        uint32_t pressed_mask = esp_pocket_audio_switch_pressed_mask();
        if (pressed_mask != last_pressed_mask) {
            ESP_LOGI(TAG, "pressed mask: 0x%08" PRIx32, pressed_mask);
            last_pressed_mask = pressed_mask;
        }

        if ((int32_t)(now - next_joystick_log) >= 0) {
            esp_err_t err = esp_pocket_audio_joystick_read(&joystick);
            if (err == ESP_OK) {
                ESP_LOGI(
                    TAG,
                    "joystick dir=%s x raw=%d norm=%.3f, y raw=%d norm=%.3f",
                    esp_pocket_audio_joystick_direction_name(joystick.direction),
                    joystick.x.raw,
                    joystick.x.value,
                    joystick.y.raw,
                    joystick.y.value
                );
            }
            next_joystick_log = now + JOYSTICK_LOG_TICKS;
        }

        if ((int32_t)(now - next_summary_log) >= 0) {
            ESP_LOGI(
                TAG,
                "summary: pressed=0x%08" PRIx32 " pcf_int=%s",
                pressed_mask,
                esp_pocket_audio_input_int_is_asserted() ? "asserted" : "idle"
            );
            update_display(hw, pressed_mask, &joystick);
            next_summary_log = now + SUMMARY_LOG_TICKS;
        }

        vTaskDelay(INPUT_POLL_TICKS);
    }
}
