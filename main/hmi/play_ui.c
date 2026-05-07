#include "hmi/play_ui.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"

static const char *TAG = "PLAY_UI";

static const TickType_t UI_REFRESH_TICKS = pdMS_TO_TICKS(100);
static const TickType_t UI_PAGE_TIMEOUT_TICKS = pdMS_TO_TICKS(20);
static const uint32_t UI_TASK_STACK_SIZE = 4096;
static const UBaseType_t UI_TASK_PRIORITY = 3;
static const BaseType_t UI_TASK_CORE = 0;

static const char *EFFECT_NAMES[PLAY_UI_EFFECT_COUNT] = {
    [PLAY_UI_EFFECT_REVERB] = "REVERB",
    [PLAY_UI_EFFECT_DELAY] = "DELAY",
    [PLAY_UI_EFFECT_CHORUS] = "CHORUS",
    [PLAY_UI_EFFECT_FILTER] = "FILTER",
    [PLAY_UI_EFFECT_JOYSTICK] = "JOYSTICK",
};

static const esp_pocket_audio_hw_t *s_hw;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static play_ui_model_t s_model;
static bool s_dirty;

const char *play_ui_effect_name(play_ui_effect_t effect)
{
    return effect < PLAY_UI_EFFECT_COUNT ? EFFECT_NAMES[effect] : "?";
}

void play_ui_model_init(play_ui_model_t *model)
{
    if (model == NULL) {
        return;
    }

    memset(model, 0, sizeof(*model));
    model->screen = PLAY_UI_SCREEN_HOME;
    model->key = SYNTH_KEY_C;
    model->scale = SYNTH_SCALE_MAJOR;
    model->modifier = SYNTH_CHORD_MOD_NONE;
    model->joystick_mode = SYNTH_JOYSTICK_MODE_DEFAULT;
    model->sound = SYNTH_SOUND_SAW;
    model->mode = SYNTH_PLAY_MODE_PLAY;
    model->bpm = 120;
    model->wheel_percent = 100;
    model->joystick_direction = CONTROL_JOYSTICK_CENTER;
    strlcpy(model->chord_name, "---", sizeof(model->chord_name));
    strlcpy(model->status, "READY", sizeof(model->status));
}

void play_ui_update(const play_ui_model_t *model)
{
    if (model == NULL || s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_model = *model;
        s_dirty = true;
        xSemaphoreGive(s_lock);
    }
}

void play_ui_show_status(const char *status)
{
    if (status == NULL || s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_model.screen = PLAY_UI_SCREEN_STATUS;
        strlcpy(s_model.status, status, sizeof(s_model.status));
        s_dirty = true;
        xSemaphoreGive(s_lock);
    }
}

static void draw_rowf(pocket_oled_t *display, int row, const char *fmt, ...)
{
    char line[24];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    pocket_oled_draw_text_row(display, row, line);
}

static char pressed_char(uint8_t held_mask, uint8_t slot)
{
    return (held_mask & (1U << slot)) != 0 ? (char)('1' + slot) : '.';
}

static void draw_status_row(pocket_oled_t *display, const play_ui_model_t *model)
{
    if (model->wheel_present) {
        draw_rowf(display, 7, "WHL %3u%% %.11s", model->wheel_percent, model->status);
    } else {
        draw_rowf(display, 7, "WHL --- %.11s", model->status);
    }
}

static void draw_header(pocket_oled_t *display, const char *title)
{
    pocket_oled_draw_text_row(display, 0, title);
    oled_display_draw_hline(display, 0, 8, POCKET_OLED_WIDTH, true);
}

static void draw_home(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_rowf(display, 0, "%s %3u", synth_play_mode_name(model->mode), model->bpm);
    draw_rowf(display, 1, "KEY %s %s O%+d",
              synth_key_name(model->key),
              synth_scale_name(model->scale),
              model->global_octave_shift);
    draw_rowf(display, 2, "SND %s", synth_sound_name(model->sound));
    draw_rowf(display, 3, "CHD %s", model->chord_name);
    draw_rowf(display, 4, "MOD %s", synth_chord_modifier_name(model->modifier));
    draw_rowf(display, 5, "JOY %.16s", controls_joystick_direction_name(model->joystick_direction));
    draw_rowf(display, 6, "HELD %c%c%c%c%c%c%c",
              pressed_char(model->held_mask, 0),
              pressed_char(model->held_mask, 1),
              pressed_char(model->held_mask, 2),
              pressed_char(model->held_mask, 3),
              pressed_char(model->held_mask, 4),
              pressed_char(model->held_mask, 5),
              pressed_char(model->held_mask, 6));
    draw_status_row(display, model);
}

static void draw_key_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "KEY SETTINGS");
    draw_rowf(display, 2, "KEY   %s", synth_key_name(model->key));
    draw_rowf(display, 3, "SCALE %s", synth_scale_name(model->scale));
    draw_rowf(display, 4, "GLOBAL OCT %+d", model->global_octave_shift);
    draw_rowf(display, 5, "BTN%u OCT %+d", model->focus_slot + 1, model->button_octave_shift[model->focus_slot]);
    pocket_oled_draw_text_row(display, 6, "L/R KEY  U/D OCT");
    draw_status_row(display, model);
}

static void draw_sound_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    play_ui_effect_t effect = (play_ui_effect_t)(model->effect_index % PLAY_UI_EFFECT_COUNT);
    draw_header(display, "SOUND FX");
    draw_rowf(display, 2, "SND %s", synth_sound_name(model->sound));
    draw_rowf(display, 3, "ENG %s", synth_sound_engine_name(model->sound));
    if (effect == PLAY_UI_EFFECT_JOYSTICK) {
        draw_rowf(display, 4, "JOY %s", synth_joystick_mode_name(model->joystick_mode));
    } else {
        draw_rowf(display, 4, "FX %s %s",
                  play_ui_effect_name(effect),
                  model->effects[effect] ? "ON" : "OFF");
    }
    pocket_oled_draw_text_row(display, 5, "1-7 QUICK SELECT");
    pocket_oled_draw_text_row(display, 6, "L/R SND U/D FX");
    draw_status_row(display, model);
}

static void draw_mode_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "MODE TEMPO");
    draw_rowf(display, 2, "MODE %s", synth_play_mode_name(model->mode));
    draw_rowf(display, 3, "BPM  %u", model->bpm);
    pocket_oled_draw_text_row(display, 4, "LOOPER OFF");
    pocket_oled_draw_text_row(display, 5, "DRUMS  OFF");
    pocket_oled_draw_text_row(display, 6, "L/R BPM U/D MODE");
    draw_status_row(display, model);
}

static void draw_status(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "STATUS");
    draw_rowf(display, 3, "%.21s", model->status);
}

static void draw_display(pocket_oled_t *display, const play_ui_model_t *model)
{
    pocket_oled_clear(display);
    switch (model->screen) {
    case PLAY_UI_SCREEN_KEY:
        draw_key_menu(display, model);
        break;
    case PLAY_UI_SCREEN_SOUND:
        draw_sound_menu(display, model);
        break;
    case PLAY_UI_SCREEN_MODE:
        draw_mode_menu(display, model);
        break;
    case PLAY_UI_SCREEN_STATUS:
        draw_status(display, model);
        break;
    case PLAY_UI_SCREEN_HOME:
    default:
        draw_home(display, model);
        break;
    }
}

static void play_ui_task(void *arg)
{
    (void)arg;

    while (true) {
        play_ui_model_t model = {0};
        bool should_draw = false;

        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            model = s_model;
            should_draw = s_dirty;
            s_dirty = false;
            xSemaphoreGive(s_lock);
        }

        if (should_draw && s_hw != NULL && pocket_oled_is_initialized(&s_hw->display)) {
            pocket_oled_t *display = (pocket_oled_t *)&s_hw->display;
            draw_display(display, &model);
            esp_err_t err = oled_display_flush_cooperatively(display, UI_PAGE_TIMEOUT_TICKS);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "OLED flush skipped: %s", esp_err_to_name(err));
            }
        }

        vTaskDelay(UI_REFRESH_TICKS);
    }
}

esp_err_t play_ui_start(const esp_pocket_audio_hw_t *hw)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_hw = hw;
    play_ui_model_init(&s_model);
    s_dirty = true;

    BaseType_t task_created = xTaskCreatePinnedToCore(
        play_ui_task,
        "play_ui",
        UI_TASK_STACK_SIZE,
        NULL,
        UI_TASK_PRIORITY,
        &s_task,
        UI_TASK_CORE
    );
    if (task_created != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_hw = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "play UI started");
    return ESP_OK;
}
