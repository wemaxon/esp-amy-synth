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
    [PLAY_UI_EFFECT_TREMOLO] = "TREMOLO",
    [PLAY_UI_EFFECT_FLANGER] = "FLANGER",
    [PLAY_UI_EFFECT_LFO] = "LFO",
    [PLAY_UI_EFFECT_JOYSTICK] = "JOYSTICK",
};

static const char *LFO_LEVEL_NAMES[] = {
    "OFF", "LOW", "MED", "HIGH",
};

static const char *DELAY_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T",
};

static const char *TREMOLO_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T", "1/32",
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
    model->strum_speed = SYNTH_STRUM_SPEED_MEDIUM;
    model->arp_pattern = SYNTH_ARP_PATTERN_UP;
    model->arp_rate = SYNTH_RHYTHM_RATE_EIGHTH;
    model->arp_chord_mode = SYNTH_ARP_CHORD_MODE_ARP_ONLY;
    model->repeat_rate = SYNTH_RHYTHM_RATE_EIGHTH;
    model->effect_index = PLAY_UI_EFFECT_JOYSTICK;
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

static void draw_header(pocket_oled_t *display, const char *title)
{
    pocket_oled_draw_text_row(display, 0, title);
    oled_display_draw_hline(display, 0, 8, POCKET_OLED_WIDTH, true);
}

static void draw_home(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_rowf(display, 3, "%.21s", model->chord_name);
}

static void draw_mode_param(pocket_oled_t *display, int row, const play_ui_model_t *model)
{
    switch (model->mode) {
    case SYNTH_PLAY_MODE_STRUM:
        draw_rowf(display,
                  row,
                  "SPEED %s %ums",
                  synth_strum_speed_name(model->strum_speed),
                  synth_strum_speed_delay_ms(model->strum_speed));
        break;
    case SYNTH_PLAY_MODE_ARPEGGIO:
        draw_rowf(display,
                  row,
                  "%s %s",
                  synth_arp_pattern_name(model->arp_pattern),
                  synth_rhythm_rate_name(model->arp_rate));
        draw_rowf(display,
                  row + 1,
                  "%.21s",
                  synth_arp_chord_mode_name(model->arp_chord_mode));
        break;
    case SYNTH_PLAY_MODE_REPEAT:
        draw_rowf(display, row, "RATE %s", synth_rhythm_rate_name(model->repeat_rate));
        break;
    case SYNTH_PLAY_MODE_PLAY:
        pocket_oled_draw_text_row(display, row, "PRESS = CHORD");
        break;
    case SYNTH_PLAY_MODE_LEAD:
        pocket_oled_draw_text_row(display, row, "ROOT NOTE ONLY");
        break;
    case SYNTH_PLAY_MODE_DRONE:
        pocket_oled_draw_text_row(display, row, "TOGGLE CHORD");
        break;
    default:
        pocket_oled_draw_text_row(display, row, "WARN: NOT IMPLEMENTED");
        break;
    }
}

static void draw_key_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "KEY SETTINGS");
    draw_rowf(display, 2, "KEY   %s", synth_key_name(model->key));
    draw_rowf(display, 3, "SCALE %s", synth_scale_name(model->scale));
    draw_rowf(display, 4, "GLOBAL OCT %+d", model->global_octave_shift);
    draw_rowf(display, 5, "BTN%u OCT %+d", model->focus_slot + 1, model->button_octave_shift[model->focus_slot]);
}

static void draw_sound_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    play_ui_effect_t effect = (play_ui_effect_t)(model->effect_index % PLAY_UI_EFFECT_COUNT);
    draw_header(display, "SOUND FX");
    draw_rowf(display, 2, "SND %s", synth_sound_name(model->sound));
    draw_rowf(display, 3, "ENG %s", synth_sound_engine_name(model->sound));
    if (effect == PLAY_UI_EFFECT_JOYSTICK) {
        draw_rowf(display, 4, "JOY %s", synth_joystick_mode_name(model->joystick_mode));
    } else if (effect == PLAY_UI_EFFECT_DELAY) {
        uint8_t delay_level = model->delay_level;
        if (delay_level >= sizeof(DELAY_LEVEL_NAMES) / sizeof(DELAY_LEVEL_NAMES[0])) {
            delay_level = 0;
        }
        draw_rowf(display, 4, "FX DELAY %s", DELAY_LEVEL_NAMES[delay_level]);
    } else if (effect == PLAY_UI_EFFECT_TREMOLO) {
        uint8_t tremolo_level = model->tremolo_level;
        if (tremolo_level >= sizeof(TREMOLO_LEVEL_NAMES) / sizeof(TREMOLO_LEVEL_NAMES[0])) {
            tremolo_level = 0;
        }
        draw_rowf(display, 4, "FX TREM %s", TREMOLO_LEVEL_NAMES[tremolo_level]);
    } else if (effect == PLAY_UI_EFFECT_LFO) {
        uint8_t lfo_level = model->lfo_level;
        if (lfo_level >= sizeof(LFO_LEVEL_NAMES) / sizeof(LFO_LEVEL_NAMES[0])) {
            lfo_level = 0;
        }
        draw_rowf(display, 4, "FX LFO %s", LFO_LEVEL_NAMES[lfo_level]);
    } else {
        draw_rowf(display, 4, "FX %s %s",
                  play_ui_effect_name(effect),
                  model->effects[effect] ? "ON" : "OFF");
    }
}

static void draw_mode_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    if (model->mode_bpm_submenu) {
        draw_header(display, "BPM");
        draw_rowf(display, 2, "TEMPO %3u BPM", model->bpm);
        pocket_oled_draw_text_row(display, 4, "L/R ADJUST");
        pocket_oled_draw_text_row(display, 5, "U/D BACK");
        pocket_oled_draw_text_row(display, 6, "RANGE 40-300");
        return;
    }

    draw_header(display, "MODES");
    draw_rowf(display, 2, "%02u/%02u %s",
              (unsigned)(model->mode + 1),
              (unsigned)SYNTH_PLAY_MODE_COUNT,
              synth_play_mode_short_name(model->mode));
    draw_rowf(display, 2, "%.21s", synth_play_mode_name(model->mode));
    draw_mode_param(display, 3, model);
}

static void draw_time_ms(pocket_oled_t *display, int row, const char *prefix, uint32_t ms)
{
    uint32_t total_seconds = ms / 1000U;
    draw_rowf(display,
              row,
              "%s %02lu:%02lu",
              prefix,
              (unsigned long)(total_seconds / 60U),
              (unsigned long)(total_seconds % 60U));
}

static void draw_looper_track(pocket_oled_t *display,
                              const play_ui_model_t *model,
                              uint8_t track,
                              int row)
{
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return;
    }

    const char *marker = track == model->looper.current_track ? ">" : " ";
    uint32_t total_seconds = model->looper.track_ms[track] / 1000U;
    draw_rowf(display,
              row,
              "%sT%u %-4s %02lu:%02lu",
              marker,
              (unsigned)(track + 1),
              synth_looper_track_state_short_name(model->looper.track_state[track]),
              (unsigned long)(total_seconds / 60U),
              (unsigned long)(total_seconds % 60U));
}

static void draw_looper(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "LOOPER");
    uint8_t first_track = (uint8_t)(model->looper.page * SYNTH_LOOPER_TRACKS_PER_PAGE);
    draw_looper_track(display, model, first_track, 2);
    draw_looper_track(display, model, (uint8_t)(first_track + 1U), 3);
    if (model->looper.bar_count == 0) {
        pocket_oled_draw_text_row(display, 5, "BARS FREE");
    } else {
        draw_rowf(display, 5, "BARS %u", (unsigned)model->looper.bar_count);
    }
    if (model->looper.paused) {
        pocket_oled_draw_text_row(display, 6, "PAUSED");
    } else if (model->looper.master_ms > 0) {
        draw_time_ms(display, 6, "LEN", model->looper.master_ms);
    } else {
        draw_rowf(display, 6, "%.21s", model->looper.message);
    }
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
    case PLAY_UI_SCREEN_LOOPER:
        draw_looper(display, model);
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
