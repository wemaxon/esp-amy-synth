#include "synth/app.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "synth/amy_engine.h"
#include "synth/chord_engine.h"

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    control_joystick_direction_t last_menu_direction;
    uint8_t held_mask;
    uint8_t focus_slot;
    TaskHandle_t task;
} synth_app_state_t;

static const char *TAG = "SYNTH_APP";

static const uint32_t APP_TASK_STACK_SIZE = 4096;
static const UBaseType_t APP_TASK_PRIORITY = 5;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t APP_UPDATE_TICKS = pdMS_TO_TICKS(20);
static const int8_t GLOBAL_OCTAVE_MIN = -1;
static const int8_t GLOBAL_OCTAVE_MAX = 2;
static const int8_t BUTTON_OCTAVE_MIN = -2;
static const int8_t BUTTON_OCTAVE_MAX = 1;

static synth_app_state_t s_app = {
    .key = SYNTH_KEY_C,
    .scale = SYNTH_SCALE_MAJOR,
    .global_octave_shift = 0,
    .modifier = SYNTH_CHORD_MOD_NONE,
    .last_menu_direction = CONTROL_JOYSTICK_CENTER,
    .focus_slot = 0,
};

static uint8_t slot_from_degree(uint8_t degree)
{
    return degree > 0 ? (uint8_t)(degree - 1) : 0;
}

static bool build_chord(uint8_t degree, synth_chord_t *chord)
{
    uint8_t slot = slot_from_degree(degree);
    synth_chord_request_t request = {
        .key = s_app.key,
        .scale = s_app.scale,
        .degree = degree,
        .global_octave_shift = s_app.global_octave_shift,
        .button_octave_shift = s_app.button_octave_shift[slot],
        .modifier = s_app.modifier,
    };
    return synth_build_chord(&request, chord) == 0;
}

static void play_degree(uint8_t degree)
{
    synth_chord_t chord = {0};
    if (!build_chord(degree, &chord)) {
        ESP_LOGW(TAG, "failed to build chord degree=%u", degree);
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    esp_err_t err = synth_amy_chord_on(slot, &chord);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play chord: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "play %s key=%s octave=%d mod=%s notes=%u",
             chord.name,
             synth_key_name(s_app.key),
             s_app.global_octave_shift,
             synth_chord_modifier_name(s_app.modifier),
             chord.note_count);
}

static void refresh_held_chords(void)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) != 0) {
            play_degree((uint8_t)(slot + 1));
        }
    }
}

static void release_degree(uint8_t degree)
{
    uint8_t slot = slot_from_degree(degree);
    s_app.held_mask &= (uint8_t)~(1U << slot);
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(slot));
}

static void handle_chord_event(const control_event_t *event)
{
    uint8_t degree = 0;
    if (!keymap_switch_to_chord_degree(event->sw, &degree)) {
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
        s_app.held_mask |= 1U << slot;
        s_app.focus_slot = slot;
        play_degree(degree);
    } else {
        release_degree(degree);
    }
}

static void step_key(int8_t semitones)
{
    s_app.key = synth_key_step(s_app.key, semitones);
    ESP_LOGI(TAG, "key=%s", synth_key_name(s_app.key));
    refresh_held_chords();
}

static void step_global_octave(int8_t octaves)
{
    int8_t next = (int8_t)(s_app.global_octave_shift + octaves);
    if (next < GLOBAL_OCTAVE_MIN) {
        next = GLOBAL_OCTAVE_MIN;
    } else if (next > GLOBAL_OCTAVE_MAX) {
        next = GLOBAL_OCTAVE_MAX;
    }

    if (next == s_app.global_octave_shift) {
        return;
    }

    s_app.global_octave_shift = next;
    ESP_LOGI(TAG, "global octave=%d", s_app.global_octave_shift);
    refresh_held_chords();
}

static void step_focused_button_octave(int8_t octaves)
{
    if ((s_app.held_mask & (1U << s_app.focus_slot)) == 0) {
        return;
    }

    int8_t next = (int8_t)(s_app.button_octave_shift[s_app.focus_slot] + octaves);
    if (next < BUTTON_OCTAVE_MIN) {
        next = BUTTON_OCTAVE_MIN;
    } else if (next > BUTTON_OCTAVE_MAX) {
        next = BUTTON_OCTAVE_MAX;
    }

    if (next == s_app.button_octave_shift[s_app.focus_slot]) {
        return;
    }

    s_app.button_octave_shift[s_app.focus_slot] = next;
    ESP_LOGI(TAG, "chord %u octave=%d", (unsigned)(s_app.focus_slot + 1), next);
    play_degree((uint8_t)(s_app.focus_slot + 1));
}

static void update_key_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_key(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_key(1);
        break;
    case CONTROL_JOYSTICK_UP:
        if (s_app.held_mask != 0) {
            step_focused_button_octave(1);
        } else {
            step_global_octave(1);
        }
        break;
    case CONTROL_JOYSTICK_DOWN:
        if (s_app.held_mask != 0) {
            step_focused_button_octave(-1);
        } else {
            step_global_octave(-1);
        }
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void update_joystick(void)
{
    control_joystick_state_t joystick = {0};
    if (controls_joystick_read(&joystick) != ESP_OK) {
        return;
    }

    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        update_key_menu(joystick.direction);
        return;
    }
    s_app.last_menu_direction = CONTROL_JOYSTICK_CENTER;

    synth_chord_modifier_t modifier = synth_chord_modifier_from_joystick(joystick.direction);
    if (modifier == s_app.modifier) {
        return;
    }

    s_app.modifier = modifier;
    ESP_LOGI(TAG, "joystick modifier=%s", synth_chord_modifier_name(modifier));
    refresh_held_chords();
}

static void app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "chord synth app started key=%s scale=%s",
             synth_key_name(s_app.key),
             synth_scale_name(s_app.scale));

    while (true) {
        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            handle_chord_event(&event);
        }

        update_joystick();
        vTaskDelay(APP_UPDATE_TICKS);
    }
}

esp_err_t synth_app_start(const esp_pocket_audio_hw_t *hw)
{
    if (s_app.task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(synth_amy_init(hw), TAG, "AMY synth init failed");

    BaseType_t task_created = xTaskCreatePinnedToCore(
        app_task,
        "synth_app",
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        &s_app.task,
        APP_TASK_CORE
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
