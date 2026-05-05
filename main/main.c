#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "es8388.h"
#include "esp_pocket_audio.h"
#include "esp_pocket_audio_input.h"

static const char *TAG = "main";

static const uint32_t APP_TASK_STACK_SIZE = 8192;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t INPUT_POLL_TICKS = pdMS_TO_TICKS(20);
static const TickType_t JOYSTICK_LOG_TICKS = pdMS_TO_TICKS(250);
static const TickType_t SUMMARY_LOG_TICKS = pdMS_TO_TICKS(1000);

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
    case ESP_POCKET_AUDIO_SWITCH_KEY_MENU:
        return "KEY_MENU";
    case ESP_POCKET_AUDIO_SWITCH_TEMPO_MENU:
        return "KEY_TEMPO";
    case ESP_POCKET_AUDIO_SWITCH_SOUND_MENU:
        return "KEY_SOUND";
    default:
        return "UNKNOWN";
    }
}

static void log_setup(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_POCKET_AUDIO", ESP_LOG_VERBOSE);
    esp_log_level_set("ESP_POCKET_AUDIO_INPUT", ESP_LOG_VERBOSE);
    esp_log_level_set("ES8388", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
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

static void diagnostic_loop(void)
{
    TickType_t next_joystick_log = 0;
    TickType_t next_summary_log = 0;
    uint32_t last_pressed_mask = UINT32_MAX;

    while (true) {
        TickType_t now = xTaskGetTickCount();

        esp_err_t err = esp_pocket_audio_input_update();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "input update failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

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
            esp_pocket_audio_joystick_state_t joystick = {0};
            err = esp_pocket_audio_joystick_read(&joystick);
            if (err == ESP_OK) {
                ESP_LOGI(
                    TAG,
                    "joystick x raw=%d norm=%.3f, y raw=%d norm=%.3f",
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
            next_summary_log = now + SUMMARY_LOG_TICKS;
        }

        vTaskDelay(INPUT_POLL_TICKS);
    }
}

static void app_task(void *arg)
{
    (void)arg;

    log_setup();
    ESP_LOGI(TAG, "esp-pocket-audio IDF-native hardware diagnostic starting");

    init_nvs();
    ESP_ERROR_CHECK(esp_pocket_audio_init());

    const esp_pocket_audio_hw_t *hw = esp_pocket_audio_hw();
    ESP_ERROR_CHECK(hw != NULL ? ESP_OK : ESP_ERR_INVALID_STATE);
    ESP_ERROR_CHECK(es8388_dump_registers((es8388_t *)&hw->codec, TAG));

    diagnostic_loop();
}

void app_main(void)
{
    BaseType_t task_created = xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        APP_TASK_STACK_SIZE,
        NULL,
        5,
        NULL,
        APP_TASK_CORE
    );

    ESP_ERROR_CHECK((task_created == pdPASS) ? ESP_OK : ESP_FAIL);
}
