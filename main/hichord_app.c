#include "hichord_app.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "hichord_synth_amy.h"
#include "hichord_theory.h"

static const char *TAG = "HICHORD_APP";

static const uint32_t APP_TASK_STACK_SIZE = 4096;
static const UBaseType_t APP_TASK_PRIORITY = 5;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t APP_UPDATE_TICKS = pdMS_TO_TICKS(20);
static const uint8_t DEFAULT_KEY_ROOT_MIDI = 60;

typedef struct {
    uint8_t key_root_midi;
    bool chord_held;
    uint8_t held_degree;
    hichord_quality_t active_quality;
    TaskHandle_t task;
} hichord_app_state_t;

static hichord_app_state_t s_app = {
    .key_root_midi = DEFAULT_KEY_ROOT_MIDI,
    .active_quality = HICHORD_QUALITY_DIATONIC,
};

static void play_degree(uint8_t degree, hichord_quality_t quality)
{
    hichord_chord_t chord = {0};
    if (hichord_chord_for_degree(s_app.key_root_midi, degree, quality, &chord) != 0) {
        ESP_LOGW(TAG, "failed to build chord degree=%u quality=%s",
                 degree,
                 hichord_quality_name(quality));
        return;
    }

    esp_err_t err = hichord_synth_amy_play_chord(&chord);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play chord: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "degree=%u quality=%s notes=%u",
             degree,
             hichord_quality_name(quality),
             chord.note_count);
}

static void handle_input_event(const control_event_t *event)
{
    uint8_t degree = 0;
    if (!keymap_switch_to_chord_degree(event->sw, &degree)) {
        return;
    }

    if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
        /* PLAY mode uses chord replacement: the most recent chord button owns the active voices. */
        s_app.chord_held = true;
        s_app.held_degree = degree;
        play_degree(degree, s_app.active_quality);
        return;
    }

    if (s_app.chord_held && s_app.held_degree == degree) {
        s_app.chord_held = false;
        s_app.held_degree = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(hichord_synth_amy_release_chord());
    }
}

static void update_joystick_quality(void)
{
    control_joystick_state_t joystick = {0};
    if (controls_joystick_read(&joystick) != ESP_OK) {
        return;
    }

    hichord_quality_t quality = hichord_quality_from_joystick(joystick.direction);
    if (quality == s_app.active_quality) {
        return;
    }

    s_app.active_quality = quality;
    ESP_LOGI(TAG, "joystick quality=%s", hichord_quality_name(quality));
    if (s_app.chord_held) {
        play_degree(s_app.held_degree, quality);
    }
}

static void app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "HiChord play app started");

    while (true) {
        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            handle_input_event(&event);
        }

        update_joystick_quality();
        vTaskDelay(APP_UPDATE_TICKS);
    }
}

esp_err_t hichord_app_start(const esp_pocket_audio_hw_t *hw)
{
    if (s_app.task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(hichord_synth_amy_init(hw), TAG, "AMY synth init failed");

    BaseType_t task_created = xTaskCreatePinnedToCore(
        app_task,
        "hichord_app",
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        &s_app.task,
        APP_TASK_CORE
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
