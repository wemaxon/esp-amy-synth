#include "hmi/play_ui.h"
#include "esp_pocket_audio.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"
#include "synth/app/key.h"
#include "synth/app/settings.h"

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
    [PLAY_UI_EFFECT_STEREO] = "STEREO",
    [PLAY_UI_EFFECT_FILTER] = "FILTER",
    [PLAY_UI_EFFECT_TREMOLO] = "TREMOLO",
    [PLAY_UI_EFFECT_FLANGER] = "FLANGER",
    [PLAY_UI_EFFECT_LFO] = "LFO",
    [PLAY_UI_EFFECT_JOYSTICK] = "JOYSTICK",
    [PLAY_UI_EFFECT_VOICE_LEADING] = "VOICE LEAD",
    [PLAY_UI_EFFECT_BASS_MODE] = "BASS MODE",
    [PLAY_UI_EFFECT_VOICE_COUNT] = "VOICES",
    [PLAY_UI_EFFECT_ADSR] = "ADSR",
    [PLAY_UI_EFFECT_GLIDE] = "GLIDE",
};

static const char *LFO_LEVEL_NAMES[] = {
    "OFF", "LOW", "MED", "HIGH",
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
    model->global_octave_shift = SYNTH_APP_GLOBAL_OCTAVE_DEFAULT;
    model->modifier = SYNTH_CHORD_MOD_NONE;
    model->joystick_mode = SYNTH_JOYSTICK_MODE_DEFAULT;
    model->bass_mode = SYNTH_BASS_MODE_OFF;
    model->voice_leading_enabled = false;
    model->chord_lock_enabled = false;
    model->sound = SYNTH_SOUND_SAW;
    model->mode = SYNTH_PLAY_MODE_PLAY;
    model->bpm = 120;
    model->strum_speed = SYNTH_STRUM_SPEED_MEDIUM;
    model->arp_pattern = SYNTH_ARP_PATTERN_UP;
    model->arp_rate = SYNTH_RHYTHM_RATE_EIGHTH;
    model->arp_chord_mode = SYNTH_ARP_CHORD_MODE_ARP_ONLY;
    model->repeat_rate = SYNTH_RHYTHM_RATE_EIGHTH;
    model->drum_kit = SYNTH_DRUM_KIT_TIGHT;
    model->drum_loop_style = SYNTH_DRUM_LOOP_ROCK;
    model->drum_loop_variation = SYNTH_DRUM_LOOP_VARIATION_ORIGINAL;
    strlcpy(model->drum_auto_rate, "-", sizeof(model->drum_auto_rate));
    model->effect_index = PLAY_UI_EFFECT_JOYSTICK;
    model->effects[PLAY_UI_EFFECT_STEREO] = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0;
    model->adsr_preset = 4;
    model->attack_ms = 25;
    model->release_ms = 450;
    model->glide_level = 0;
    model->wheel_percent = 100;
    model->volume_percent = 100;
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


static void draw_header(pocket_oled_t *display, const char *title)
{
    pocket_oled_draw_text_row(display, 0, title);
    oled_display_draw_hline(display, 0, 8, POCKET_OLED_WIDTH, true);
}

static void draw_volume(pocket_oled_t *display, const play_ui_model_t *model)
{
    uint8_t percent = model->volume_percent;
    if (percent > 100) {
        percent = 100;
    }

    char line[5];
    snprintf(line, sizeof(line), "V%3u", (unsigned)percent);
    pocket_oled_draw_text(display, POCKET_OLED_WIDTH - 24, 0, line);
}

static void draw_home(pocket_oled_t *display, const play_ui_model_t *model)
{
    if (model->mode == SYNTH_PLAY_MODE_MIC_SAMPLE &&
        synth_play_mode_is_implemented(model->mode)) {
        draw_header(display, "MIC SAMPLE");
        float gain = synth_mic_gain_from_percent(model->mic_gain_percent);
        oled_display_draw_rowf(display, 2, "GAIN %.1fx", (double)gain);
        uint32_t elapsed_ms = (model->mic_sample.frames * 1000U) /
                              ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ;
        uint32_t capacity_ms = (model->mic_sample.capacity_frames * 1000U) /
                               ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ;
        if (model->mic_sample.state == SYNTH_MIC_SAMPLE_RECORDING) {
            oled_display_draw_rowf(display, 3, "REC %lu.%lu/%lu.%lus",
                      (unsigned long)(elapsed_ms / 1000U),
                      (unsigned long)((elapsed_ms % 1000U) / 100U),
                      (unsigned long)(capacity_ms / 1000U),
                      (unsigned long)((capacity_ms % 1000U) / 100U));
            oled_display_draw_rowf(display, 4, "PEAK %u%% CLIP %lu",
                      (unsigned)(((uint32_t)model->mic_sample.peak * 100U) / 32768U),
                      (unsigned long)model->mic_sample.clipped_samples);
            pocket_oled_draw_text_row(display, 6, "RELEASE BTN1 TO STOP");
        } else if (model->mic_sample.state == SYNTH_MIC_SAMPLE_ANALYZING) {
            pocket_oled_draw_text_row(display, 3, "ANALYZING PITCH...");
        } else if (model->mic_sample.state == SYNTH_MIC_SAMPLE_ERROR) {
            pocket_oled_draw_text_row(display, 3, "CAPTURE FAILED");
            pocket_oled_draw_text_row(display, 6, "HOLD BTN1 TO RETRY");
        } else {
            pocket_oled_draw_text_row(display, 3, "HOLD BTN1 TO RECORD");
            pocket_oled_draw_text_row(display, 4, "MAX 3.0 SECONDS");
            pocket_oled_draw_text_row(display, 6, "WHEEL = MIC GAIN");
        }
        return;
    }

    size_t length = strnlen(model->chord_name, sizeof(model->chord_name));
    uint8_t scale = length <= 10 ? 2 : 1;
    int width = (int)length * 6 * (int)scale;
    int x = (POCKET_OLED_WIDTH - width) / 2;
    if (x < 0) {
        x = 0;
    }
    pocket_oled_draw_text_scaled(display, x, scale == 2 ? 24 : 28, model->chord_name, scale);
}

static void draw_mode_param(pocket_oled_t *display, int row, const play_ui_model_t *model)
{
    switch (model->mode) {
    case SYNTH_PLAY_MODE_STRUM:
        oled_display_draw_rowf(display,
                  row,
                  "SPEED %s %ums",
                  synth_strum_speed_name(model->strum_speed),
                  synth_strum_speed_delay_ms(model->strum_speed));
        break;
    case SYNTH_PLAY_MODE_ARPEGGIO:
        oled_display_draw_rowf(display,
                  row,
                  "%s %s",
                  synth_arp_pattern_name(model->arp_pattern),
                  synth_rhythm_rate_name(model->arp_rate));
        oled_display_draw_rowf(display,
                  row + 1,
                  "%.21s",
                  synth_arp_chord_mode_name(model->arp_chord_mode));
        break;
    case SYNTH_PLAY_MODE_REPEAT:
        oled_display_draw_rowf(display, row, "RATE %s", synth_rhythm_rate_name(model->repeat_rate));
        break;
    case SYNTH_PLAY_MODE_MIC_SAMPLE:
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        oled_display_draw_rowf(display,
                  row,
                  "GAIN %.1fx",
                  (double)synth_mic_gain_from_percent(model->mic_gain_percent));
        pocket_oled_draw_text_row(display, row + 1, "HOLD BTN1 TO RECORD");
#else
        pocket_oled_draw_text_row(display, row, "NOT AVAILABLE");
#endif
        break;
    case SYNTH_PLAY_MODE_DRUM_MODE:
        oled_display_draw_rowf(display, row, "KIT %s", synth_drum_kit_name(model->drum_kit));
        pocket_oled_draw_text_row(display, row + 1, "1-7 DRUM PADS");
        break;
    case SYNTH_PLAY_MODE_DRUM_LOOPS:
        oled_display_draw_rowf(display,
                  row,
                  "%s %s",
                  synth_drum_loop_style_name(model->drum_loop_style),
                  model->drum_loop_running ? "ON" : "OFF");
        oled_display_draw_rowf(display,
                  row + 1,
                  "%s KIT %s",
                  synth_drum_loop_variation_short_name(model->drum_loop_variation),
                  synth_drum_kit_name(model->drum_kit));
        break;
    case SYNTH_PLAY_MODE_AUTO_DRUM:
        oled_display_draw_rowf(display, row, "KIT %s", synth_drum_kit_name(model->drum_kit));
        oled_display_draw_rowf(display, row + 1, "RATE %.7s", model->drum_auto_rate);
        break;
    case SYNTH_PLAY_MODE_MIXER:
        oled_display_draw_rowf(display, row, "LOOP TRACK MIX");
        oled_display_draw_rowf(display, row + 1, "MET %s", model->looper.metronome_enabled ? "ON" : "OFF");
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
    oled_display_draw_rowf(display, 2, "KEY   %s", synth_key_name(model->key));
    oled_display_draw_rowf(display, 3, "GLOBAL OCT %+d", model->global_octave_shift);
    oled_display_draw_rowf(display, 4, "BTN%u OCT %+d", model->focus_slot + 1, model->button_octave_shift[model->focus_slot]);
    oled_display_draw_rowf(display, 5, "LOCK %s", model->chord_lock_enabled ? "ON" : "OFF");
}

static void draw_sound_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    play_ui_effect_t effect = (play_ui_effect_t)(model->effect_index % PLAY_UI_EFFECT_COUNT);
    draw_header(display, "SOUND FX");
    oled_display_draw_rowf(display, 2, "SND %s", synth_sound_name(model->sound));
    oled_display_draw_rowf(display, 3, "ENG %s", synth_sound_engine_name(model->sound));
    if (effect == PLAY_UI_EFFECT_JOYSTICK) {
        oled_display_draw_rowf(display, 4, "JOY %s", synth_joystick_mode_name(model->joystick_mode));
    } else if (effect == PLAY_UI_EFFECT_VOICE_LEADING) {
        oled_display_draw_rowf(display, 4, "VOICE LEAD %s", model->voice_leading_enabled ? "ON" : "OFF");
    } else if (effect == PLAY_UI_EFFECT_BASS_MODE) {
        oled_display_draw_rowf(display, 4, "BASS %s", synth_bass_mode_name(model->bass_mode));
    } else if (effect == PLAY_UI_EFFECT_VOICE_COUNT) {
        oled_display_draw_rowf(display, 4, "VOICES %s", synth_app_voice_level_name(model->voice_level));
    } else if (effect == PLAY_UI_EFFECT_ADSR) {
        oled_display_draw_rowf(display, 4, "ADSR %s", synth_app_adsr_preset_name(model->adsr_preset));
        oled_display_draw_rowf(display, 5, "A%lu R%lu",
                  (unsigned long)model->attack_ms,
                  (unsigned long)model->release_ms);
    } else if (effect == PLAY_UI_EFFECT_GLIDE) {
        oled_display_draw_rowf(display, 4, "GLIDE %s", synth_app_glide_level_name(model->glide_level));
    } else if (effect == PLAY_UI_EFFECT_DELAY) {
        oled_display_draw_rowf(display, 4, "FX DELAY %s", synth_app_delay_level_name(model->delay_level));
    } else if (effect == PLAY_UI_EFFECT_TREMOLO) {
        oled_display_draw_rowf(display, 4, "FX TREM %s", synth_app_tremolo_level_name(model->tremolo_level));
    } else if (effect == PLAY_UI_EFFECT_LFO) {
        uint8_t lfo_level = model->lfo_level;
        if (lfo_level >= sizeof(LFO_LEVEL_NAMES) / sizeof(LFO_LEVEL_NAMES[0])) {
            lfo_level = 0;
        }
        oled_display_draw_rowf(display, 4, "FX LFO %s", LFO_LEVEL_NAMES[lfo_level]);
    } else {
        oled_display_draw_rowf(display, 4, "FX %s %s",
                  play_ui_effect_name(effect),
                  model->effects[effect] ? "ON" : "OFF");
    }
}

static void draw_mode_menu(pocket_oled_t *display, const play_ui_model_t *model)
{
    if (model->mode_bpm_submenu) {
        draw_header(display, "BPM");
        oled_display_draw_rowf(display, 2, "TEMPO %3u BPM", model->bpm);
        pocket_oled_draw_text_row(display, 4, "L/R ADJUST");
        pocket_oled_draw_text_row(display, 5, "U/D BACK");
        pocket_oled_draw_text_row(display, 6, "RANGE 40-300");
        return;
    }

    draw_header(display, "MODES");
    oled_display_draw_rowf(display, 2, "%02u/%02u %s",
              (unsigned)(model->mode + 1),
              (unsigned)SYNTH_PLAY_MODE_COUNT,
              synth_play_mode_short_name(model->mode));
    oled_display_draw_rowf(display, 2, "%.21s", synth_play_mode_name(model->mode));
    draw_mode_param(display, 3, model);
}

static void draw_time_ms(pocket_oled_t *display, int row, const char *prefix, uint32_t ms)
{
    uint32_t total_seconds = ms / 1000U;
    oled_display_draw_rowf(display,
              row,
              "%s %02lu:%02lu",
              prefix,
              (unsigned long)(total_seconds / 60U),
              (unsigned long)(total_seconds % 60U));
}

static bool draw_looper_progress(pocket_oled_t *display, const play_ui_model_t *model)
{
    uint8_t track = model->looper.current_track;
    synth_looper_track_state_t state = model->looper.track_state[track];
    if (model->looper.current_track_armed &&
        !model->looper.waiting_for_sync &&
        model->looper.wait_ms > 0) {
        oled_display_draw_rowf(display,
                  4,
                  "COUNT %lu.%lus",
                  (unsigned long)(model->looper.wait_ms / 1000U),
                  (unsigned long)((model->looper.wait_ms % 1000U) / 100U));
        return true;
    }

    if (model->looper.waiting_for_sync && model->looper.progress_total_ms > 0) {
        uint32_t pos_seconds = model->looper.progress_ms / 1000U;
        uint32_t total_seconds = model->looper.progress_total_ms / 1000U;
        oled_display_draw_rowf(display,
                  4,
                  "SYNC %02lu:%02lu/%02lu:%02lu",
                  (unsigned long)(pos_seconds / 60U),
                  (unsigned long)(pos_seconds % 60U),
                  (unsigned long)(total_seconds / 60U),
                  (unsigned long)(total_seconds % 60U));
        return true;
    }

    if (model->looper.progress_total_ms > 0) {
        const char *prefix = state == SYNTH_LOOPER_TRACK_RECORDING ? "REC" : "POS";
        uint32_t pos_seconds = model->looper.progress_ms / 1000U;
        uint32_t total_seconds = model->looper.progress_total_ms / 1000U;
        oled_display_draw_rowf(display,
                  4,
                  "%s %02lu:%02lu/%02lu:%02lu",
                  prefix,
                  (unsigned long)(pos_seconds / 60U),
                  (unsigned long)(pos_seconds % 60U),
                  (unsigned long)(total_seconds / 60U),
                  (unsigned long)(total_seconds % 60U));
        return true;
    }

    if (state == SYNTH_LOOPER_TRACK_RECORDING) {
        draw_time_ms(display, 4, "REC", model->looper.track_position_ms[track]);
        return true;
    }
    return false;
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
    char flag = model->looper.track_soloed[track] ? 'S' :
                (model->looper.track_muted[track] ? 'M' : ' ');
    uint32_t total_seconds = model->looper.track_ms[track] / 1000U;
    oled_display_draw_rowf(display,
              row,
              "%sT%u%c %-4s %02lu:%02lu V%3u",
              marker,
              (unsigned)(track + 1),
              flag,
              synth_looper_track_state_short_name(model->looper.track_state[track]),
              (unsigned long)(total_seconds / 60U),
              (unsigned long)(total_seconds % 60U),
              (unsigned)model->looper.track_volume_percent[track]);
}

static void draw_looper_meter(pocket_oled_t *display, const play_ui_model_t *model, int row)
{
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    const synth_looper_meter_t *meter = &model->looper.meter;
    const char *limit = meter->clipped_samples > 0 ? "!" : "";
    oled_display_draw_rowf(display,
              row,
              "M IN%3u LP%3u O%3u%s",
              (unsigned)meter->live_peak_percent,
              (unsigned)meter->loop_peak_percent,
              (unsigned)meter->output_peak_percent,
              limit);
#else
    oled_display_draw_rowf(display, row, "%.21s", model->looper.message);
#endif
}

static void draw_looper(pocket_oled_t *display, const play_ui_model_t *model)
{
    bool mixer = model->mode == SYNTH_PLAY_MODE_MIXER;
    draw_header(display, mixer ? "MIXER" : "LOOPER");
    uint8_t first_track = (uint8_t)(model->looper.page * SYNTH_LOOPER_TRACKS_PER_PAGE);
    draw_looper_track(display, model, first_track, 2);
    draw_looper_track(display, model, (uint8_t)(first_track + 1U), 3);
    bool drew_progress = draw_looper_progress(display, model);
    bool current_waiting = model->looper.track_state[model->looper.current_track] ==
                           SYNTH_LOOPER_TRACK_WAITING;
    if (!current_waiting) {
        draw_looper_meter(display, model, 5);
    } else if (model->looper.bar_count == 0) {
        oled_display_draw_rowf(display,
                  5,
                  "BARS FREE MET %s",
                  model->looper.record_metronome_enabled ? "ON" : "OFF");
    } else {
        oled_display_draw_rowf(display,
                  5,
                  "BARS %u MET %s",
                  (unsigned)model->looper.bar_count,
                  model->looper.record_metronome_enabled ? "ON" : "OFF");
    }
    if (mixer) {
        oled_display_draw_rowf(display,
                  6,
                  "%s MET %s",
                  model->looper.paused ? "PAUSED" : (model->looper.solo_active ? "SOLO" : "PLAY"),
                  model->looper.metronome_enabled ? "ON" : "OFF");
    } else if (current_waiting) {
        oled_display_draw_rowf(display,
                  6,
                  "REC MET %s",
                  model->looper.record_metronome_enabled ? "ON" : "OFF");
    } else if (model->looper.paused) {
        pocket_oled_draw_text_row(display, 6, "PAUSED");
    } else if (model->looper.master_ms > 0) {
        draw_time_ms(display, 6, "LEN", model->looper.master_ms);
    } else if (!drew_progress) {
        oled_display_draw_rowf(display, 6, "%.21s", model->looper.message);
    }
}

static void draw_status(pocket_oled_t *display, const play_ui_model_t *model)
{
    draw_header(display, "STATUS");
    oled_display_draw_rowf(display, 3, "%.21s", model->status);
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
    draw_volume(display, model);
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
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pocket_oled_is_initialized(&hw->display)) {
        ESP_LOGI(TAG, "play UI disabled: OLED display is not initialized");
        return ESP_OK;
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
