#include "synth/app.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "hmi/play_ui.h"
#include "synth/amy_engine.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_sound_t sound;
    synth_play_mode_t mode;
    uint16_t bpm;
    uint8_t effect_index;
    bool effects[PLAY_UI_EFFECT_COUNT];
    bool wheel_present;
    uint8_t wheel_percent;
    uint8_t last_applied_wheel_percent;
    control_joystick_direction_t last_menu_direction;
    control_joystick_direction_t joystick_direction;
    uint8_t held_mask;
    uint8_t focus_slot;
    char chord_name[16];
    char status[22];
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
static const uint16_t BPM_MIN = 40;
static const uint16_t BPM_MAX = 300;
static const uint8_t WHEEL_CHANGE_THRESHOLD_PERCENT = 2;

static synth_app_state_t s_app = {
    .key = SYNTH_KEY_C,
    .scale = SYNTH_SCALE_MAJOR,
    .global_octave_shift = 0,
    .modifier = SYNTH_CHORD_MOD_NONE,
    .sound = SYNTH_SOUND_SAW,
    .mode = SYNTH_PLAY_MODE_PLAY,
    .bpm = 120,
    .effect_index = PLAY_UI_EFFECT_REVERB,
    .effects = {
        [PLAY_UI_EFFECT_FILTER] = true,
    },
    .wheel_percent = 100,
    .last_applied_wheel_percent = UINT8_MAX,
    .last_menu_direction = CONTROL_JOYSTICK_CENTER,
    .joystick_direction = CONTROL_JOYSTICK_CENTER,
    .focus_slot = 0,
    .chord_name = "---",
    .status = "READY",
};

static play_ui_screen_t active_screen(void)
{
    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        return PLAY_UI_SCREEN_KEY;
    }
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        return PLAY_UI_SCREEN_SOUND;
    }
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        return PLAY_UI_SCREEN_MODE;
    }
    return PLAY_UI_SCREEN_HOME;
}

static void set_status(const char *status)
{
    if (status != NULL) {
        strlcpy(s_app.status, status, sizeof(s_app.status));
    }
}

static void update_ui(void)
{
    play_ui_model_t model = {0};
    play_ui_model_init(&model);
    model.screen = active_screen();
    model.key = s_app.key;
    model.scale = s_app.scale;
    model.global_octave_shift = s_app.global_octave_shift;
    memcpy(model.button_octave_shift, s_app.button_octave_shift, sizeof(model.button_octave_shift));
    model.modifier = s_app.modifier;
    model.sound = s_app.sound;
    model.mode = s_app.mode;
    model.bpm = s_app.bpm;
    model.held_mask = s_app.held_mask;
    model.focus_slot = s_app.focus_slot;
    model.effect_index = s_app.effect_index;
    memcpy(model.effects, s_app.effects, sizeof(model.effects));
    model.wheel_present = s_app.wheel_present;
    model.wheel_percent = s_app.wheel_percent;
    model.joystick_direction = s_app.joystick_direction;
    strlcpy(model.chord_name, s_app.chord_name, sizeof(model.chord_name));
    strlcpy(model.status, s_app.status, sizeof(model.status));
    play_ui_update(&model);
}

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
    strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
    set_status("PLAYING");
    update_ui();
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
    if (s_app.held_mask == 0) {
        set_status("READY");
    }
    update_ui();
}

static void handle_chord_event(const control_event_t *event)
{
    uint8_t degree = 0;
    if (!keymap_switch_to_chord_degree(event->sw, &degree)) {
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
        if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
            s_app.sound = synth_sound_quick_select(degree);
            ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(s_app.sound));
            set_status("SOUND SELECTED");
            refresh_held_chords();
            update_ui();
            return;
        }

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
    set_status("KEY CHANGED");
    refresh_held_chords();
    update_ui();
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
    set_status("OCTAVE CHANGED");
    refresh_held_chords();
    update_ui();
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
    set_status("BUTTON OCTAVE");
    play_degree((uint8_t)(s_app.focus_slot + 1));
    update_ui();
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

static void step_sound(int8_t steps)
{
    s_app.sound = synth_sound_step(s_app.sound, steps);
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(s_app.sound));
    set_status("SOUND CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_effect(int8_t steps)
{
    int value = (int)s_app.effect_index + steps;
    while (value < 0) {
        value += PLAY_UI_EFFECT_COUNT;
    }
    s_app.effect_index = (uint8_t)(value % PLAY_UI_EFFECT_COUNT);
    set_status("EFFECT SELECT");
    update_ui();
}

static void toggle_current_effect(void)
{
    play_ui_effect_t effect = (play_ui_effect_t)(s_app.effect_index % PLAY_UI_EFFECT_COUNT);
    s_app.effects[effect] = !s_app.effects[effect];
    esp_err_t err = ESP_OK;
    switch (effect) {
    case PLAY_UI_EFFECT_FILTER:
        err = synth_amy_set_filter_enabled(s_app.effects[effect]);
        break;
    case PLAY_UI_EFFECT_REVERB:
    case PLAY_UI_EFFECT_DELAY:
    case PLAY_UI_EFFECT_CHORUS:
    default:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply %s: %s", play_ui_effect_name(effect), esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "%s=%s", play_ui_effect_name(effect), s_app.effects[effect] ? "on" : "off");
    set_status(s_app.effects[effect] ? "FX ON" : "FX OFF");
    update_ui();
}

static void update_sound_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_sound(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_sound(1);
        break;
    case CONTROL_JOYSTICK_UP:
        step_effect(-1);
        break;
    case CONTROL_JOYSTICK_DOWN:
        step_effect(1);
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void step_bpm(int16_t delta)
{
    int next = (int)s_app.bpm + delta;
    if (next < BPM_MIN) {
        next = BPM_MIN;
    } else if (next > BPM_MAX) {
        next = BPM_MAX;
    }

    if (next == s_app.bpm) {
        return;
    }

    s_app.bpm = (uint16_t)next;
    ESP_LOGI(TAG, "bpm=%u", s_app.bpm);
    set_status("BPM CHANGED");
    update_ui();
}

static float wheel_to_filter_cutoff_hz(uint8_t percent)
{
    float t = (float)percent / 100.0f;
    return 20.0f * powf(1000.0f, t);
}

static bool wheel_percent_changed(uint8_t percent)
{
    if (s_app.last_applied_wheel_percent == UINT8_MAX) {
        return true;
    }

    uint8_t last = s_app.last_applied_wheel_percent;
    uint8_t delta = percent > last ? (uint8_t)(percent - last) : (uint8_t)(last - percent);
    return delta >= WHEEL_CHANGE_THRESHOLD_PERCENT;
}

static bool apply_wheel(const control_wheel_state_t *wheel)
{
    if (wheel == NULL || !wheel->present || !wheel_percent_changed(wheel->percent)) {
        return false;
    }

    s_app.last_applied_wheel_percent = wheel->percent;
    if (controls_switch_is_pressed(CONTROL_MODE_MENU) ||
        (controls_switch_is_pressed(CONTROL_SOUND_MENU) &&
         s_app.effect_index == PLAY_UI_EFFECT_FILTER)) {
        float cutoff_hz = wheel_to_filter_cutoff_hz(wheel->percent);
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_filter_cutoff(cutoff_hz));
        set_status("FILTER");
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_volume(wheel->value));
        set_status("VOLUME");
    }
    return true;
}

static void update_wheel(void)
{
    control_wheel_state_t wheel = {0};
    if (controls_wheel_read(&wheel) != ESP_OK) {
        return;
    }

    bool display_changed = s_app.wheel_present != wheel.present ||
        s_app.wheel_percent != wheel.percent;
    s_app.wheel_present = wheel.present;
    s_app.wheel_percent = wheel.percent;
    bool applied = apply_wheel(&wheel);
    if (display_changed || applied) {
        update_ui();
    }
}

static void step_mode(int8_t steps)
{
    s_app.mode = synth_play_mode_step(s_app.mode, steps);
    ESP_LOGI(TAG, "mode=%s", synth_play_mode_name(s_app.mode));
    set_status("MODE CHANGED");
    update_ui();
}

static void update_mode_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_bpm(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_bpm(1);
        break;
    case CONTROL_JOYSTICK_UP:
        step_mode(-1);
        break;
    case CONTROL_JOYSTICK_DOWN:
        step_mode(1);
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
    s_app.joystick_direction = joystick.direction;

    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        update_key_menu(joystick.direction);
        update_ui();
        return;
    }
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        update_sound_menu(joystick.direction);
        update_ui();
        return;
    }
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        update_mode_menu(joystick.direction);
        update_ui();
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
    update_ui();
}

static void handle_menu_event(const control_event_t *event)
{
    if (event->type != CONTROL_EVENT_SWITCH_PRESSED) {
        update_ui();
        return;
    }

    if (event->sw == CONTROL_JOYSTICK_CLICK) {
        if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
            toggle_current_effect();
        } else if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
            set_status("LOOPER WAITING");
            update_ui();
        } else if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
            set_status("KEY MENU");
            update_ui();
        }
    } else if (event->sw == CONTROL_KEY_MENU ||
               event->sw == CONTROL_SOUND_MENU ||
               event->sw == CONTROL_MODE_MENU) {
        update_ui();
    }
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
            handle_menu_event(&event);
            handle_chord_event(&event);
        }

        update_joystick();
        update_wheel();
        vTaskDelay(APP_UPDATE_TICKS);
    }
}

esp_err_t synth_app_start(const esp_pocket_audio_hw_t *hw)
{
    if (s_app.task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(synth_amy_init(hw), TAG, "AMY synth init failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_sound(s_app.sound), TAG, "AMY sound setup failed");
    ESP_RETURN_ON_ERROR(play_ui_start(hw), TAG, "play UI start failed");
    update_ui();

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
