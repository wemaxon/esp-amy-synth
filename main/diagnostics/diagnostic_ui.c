#include "diagnostics/diagnostic_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/control_i2c.h"
#include "hmi/controls.h"
#include "hmi/joystick_calibration.h"
#include "hmi/oled_display.h"
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
#include "hmi/wheel_calibration.h"
#endif
#include "pocket_oled.h"

typedef enum {
    CAL_TARGET_JOYSTICK = 0,
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
    CAL_TARGET_WHEEL,
#endif
} cal_target_t;

typedef enum {
    CAL_UI_IDLE = 0,
    CAL_UI_CAPTURE_RANGE,
    CAL_UI_SAVED,
    CAL_UI_RESET,
    CAL_UI_ERROR,
} cal_ui_state_t;

typedef enum {
    DIAG_PAGE_CONTROLS = 0,
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    DIAG_PAGE_MIC,
#endif
} diagnostic_page_t;

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
#define MIC_DIAG_BLOCK_FRAMES 256U

typedef struct {
    uint16_t ui_peak[2];
    uint16_t log_peak[2];
    uint32_t total_frames;
    uint32_t read_errors;
    TickType_t next_log;
} mic_diagnostic_t;

static int16_t s_mic_input_block[MIC_DIAG_BLOCK_FRAMES * 2U];
#endif

static const char *TAG = "DIAG_UI";
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
static const char *MIC_TAG = "DIAG_MIC";
#endif
static const TickType_t UI_REFRESH_TICKS = pdMS_TO_TICKS(100);

static void pressed_line(uint32_t mask, char *line, size_t line_size)
{
    char chord[8];
    chord[0] = (mask & (1UL << CONTROL_CHORD_1)) ? '1' : '.';
    chord[1] = (mask & (1UL << CONTROL_CHORD_2)) ? '2' : '.';
    chord[2] = (mask & (1UL << CONTROL_CHORD_3)) ? '3' : '.';
    chord[3] = (mask & (1UL << CONTROL_CHORD_4)) ? '4' : '.';
    chord[4] = (mask & (1UL << CONTROL_CHORD_5)) ? '5' : '.';
    chord[5] = (mask & (1UL << CONTROL_CHORD_6)) ? '6' : '.';
    chord[6] = (mask & (1UL << CONTROL_CHORD_7)) ? '7' : '.';
    chord[7] = '\0';

    char key = (mask & (1UL << CONTROL_KEY_MENU)) ? 'K' : '.';
    char mode = (mask & (1UL << CONTROL_MODE_MENU)) ? 'M' : '.';
    char sound = (mask & (1UL << CONTROL_SOUND_MENU)) ? 'S' : '.';
    char joy = (mask & (1UL << CONTROL_JOYSTICK_CLICK)) ? 'J' : '.';
    snprintf(line, line_size, "SW %s %c%c%c%c", chord, key, mode, sound, joy);
}

static const char *cal_target_name(cal_target_t target)
{
    switch (target) {
    case CAL_TARGET_JOYSTICK:
        return "JOY";
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
    case CAL_TARGET_WHEEL:
        return "WHL";
#endif
    default:
        return "?";
    }
}

static const char *cal_state_name(cal_ui_state_t state)
{
    switch (state) {
    case CAL_UI_IDLE:
        return "IDLE";
    case CAL_UI_CAPTURE_RANGE:
        return "RANGE";
    case CAL_UI_SAVED:
        return "SAVED";
    case CAL_UI_RESET:
        return "RESET";
    case CAL_UI_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
static uint16_t sample_magnitude(int16_t sample)
{
    int32_t value = sample;
    return (uint16_t)(value < 0 ? -value : value);
}

static uint32_t peak_percent(uint16_t peak)
{
    return ((uint32_t)peak * 100U) / 32768U;
}

static void mic_diagnostic_reset(mic_diagnostic_t *diagnostic, TickType_t now)
{
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->next_log = now + pdMS_TO_TICKS(1000);
}

static void mic_diagnostic_read(const esp_pocket_audio_hw_t *hw, mic_diagnostic_t *diagnostic)
{
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(hw->i2s_rx,
                                     s_mic_input_block,
                                     sizeof(s_mic_input_block),
                                     &bytes_read,
                                     pdMS_TO_TICKS(20));
    if (err != ESP_OK || bytes_read == 0 ||
        (bytes_read % (2U * sizeof(s_mic_input_block[0]))) != 0) {
        ++diagnostic->read_errors;
        return;
    }

    uint32_t frames = (uint32_t)(bytes_read / (2U * sizeof(s_mic_input_block[0])));
    diagnostic->total_frames += frames;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint8_t channel = 0; channel < 2U; ++channel) {
            uint16_t magnitude = sample_magnitude(s_mic_input_block[(frame * 2U) + channel]);
            if (magnitude > diagnostic->ui_peak[channel]) {
                diagnostic->ui_peak[channel] = magnitude;
            }
            if (magnitude > diagnostic->log_peak[channel]) {
                diagnostic->log_peak[channel] = magnitude;
            }
        }
    }
}

static void mic_diagnostic_log(mic_diagnostic_t *diagnostic, TickType_t now)
{
    if ((int32_t)(now - diagnostic->next_log) < 0) {
        return;
    }

    ESP_LOGI(MIC_TAG,
             "input left_peak=%u left=%lu%% right_peak=%u right=%lu%% frames=%" PRIu32
             " read_errors=%" PRIu32,
             (unsigned)diagnostic->log_peak[0],
             (unsigned long)peak_percent(diagnostic->log_peak[0]),
             (unsigned)diagnostic->log_peak[1],
             (unsigned long)peak_percent(diagnostic->log_peak[1]),
             diagnostic->total_frames,
             diagnostic->read_errors);
    diagnostic->log_peak[0] = 0;
    diagnostic->log_peak[1] = 0;
    diagnostic->next_log = now + pdMS_TO_TICKS(1000);
}

static void draw_mic_display(const esp_pocket_audio_hw_t *hw, mic_diagnostic_t *diagnostic)
{
    pocket_oled_t *display = (pocket_oled_t *)&hw->display;
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "DIAG MIC INPUT");
    oled_display_draw_rowf(display, 1, "LEFT  %05u %3lu%%",
             (unsigned)diagnostic->ui_peak[0],
             (unsigned long)peak_percent(diagnostic->ui_peak[0]));
    oled_display_draw_rowf(display, 2, "RIGHT %05u %3lu%%",
             (unsigned)diagnostic->ui_peak[1],
             (unsigned long)peak_percent(diagnostic->ui_peak[1]));
    oled_display_draw_rowf(display, 3,
             "CAPTURE SELECT %s",
             ESP_POCKET_AUDIO_MIC_INPUT_CHANNEL == ESP_POCKET_AUDIO_INPUT_CHANNEL_RIGHT ?
                 "RIGHT" : "LEFT");
    oled_display_draw_rowf(display, 4, "FRAMES %" PRIu32, diagnostic->total_frames);
    oled_display_draw_rowf(display, 5, "RX ERR %" PRIu32, diagnostic->read_errors);
    pocket_oled_draw_text_row(display, 6, "RAW ADC, NO DIG GAIN");
    pocket_oled_draw_text_row(display, 7, "MODE = CONTROLS");

    diagnostic->ui_peak[0] = 0;
    diagnostic->ui_peak[1] = 0;
}
#endif

static void draw_display(const esp_pocket_audio_hw_t *hw,
                         diagnostic_page_t page,
                         cal_target_t target,
                         cal_ui_state_t state
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
                         , mic_diagnostic_t *mic
#endif
                         )
{
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    if (page == DIAG_PAGE_MIC) {
        draw_mic_display(hw, mic);
        esp_err_t err = oled_display_flush_cooperatively((pocket_oled_t *)&hw->display,
                                                         pdMS_TO_TICKS(25));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OLED flush skipped: %s", esp_err_to_name(err));
        }
        return;
    }
#else
    (void)page;
#endif

    control_joystick_state_t joystick = {0};
    controls_joystick_read(&joystick);
    control_wheel_state_t wheel = {0};
    controls_wheel_read(&wheel);

    char line[24];
    pocket_oled_t *display = (pocket_oled_t *)&hw->display;
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "DIAG INPUT");

    pressed_line(controls_pressed_mask(), line, sizeof(line));
    pocket_oled_draw_text_row(display, 1, line);

    int norm_x = (int)(joystick.x.value * 100.0f);
    int norm_y = (int)(joystick.y.value * 100.0f);
    oled_display_draw_rowf(display, 2, "X %05d %+04d", joystick.x.raw, norm_x);
    oled_display_draw_rowf(display, 3, "Y %05d %+04d", joystick.y.raw, norm_y);

    oled_display_draw_rowf(display, 4, "DIR %.16s", controls_joystick_direction_name(joystick.direction));

    oled_display_draw_rowf(display, 5, "CAL %s %s", cal_target_name(target), cal_state_name(state));
    if (wheel.present) {
        if (wheel.type == CONTROL_WHEEL_ABSOLUTE) {
            snprintf(line, sizeof(line), "WHL %05d %3u%%",
                     wheel.data.absolute.raw,
                     wheel.data.absolute.percent);
        } else {
            snprintf(line, sizeof(line), "ENC %" PRId32, wheel.data.relative.position);
        }
    } else {
        snprintf(line, sizeof(line), "WHL disabled");
    }
    pocket_oled_draw_text_row(display, 6, line);
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    pocket_oled_draw_text_row(display, 7, "M=mic J=cal S/K");
#elif ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
    pocket_oled_draw_text_row(display, 7, target == CAL_TARGET_JOYSTICK ?
                              "M=whl J=ctr S/K" :
                              "M=joy J=rng S/K");
#else
    pocket_oled_draw_text_row(display, 7, "J=joy cal S/K");
#endif

    esp_err_t err = oled_display_flush_cooperatively(display, pdMS_TO_TICKS(25));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED flush skipped: %s", esp_err_to_name(err));
    }
}

static void log_event(const control_event_t *event)
{
    ESP_LOGI(TAG, "%s %s",
             controls_switch_name(event->sw),
             event->type == CONTROL_EVENT_SWITCH_PRESSED ? "pressed" : "released");
}

void diagnostic_ui_run(const esp_pocket_audio_hw_t *hw)
{
    ESP_LOGI(TAG, "diagnostic UI started");

    diagnostic_page_t page = DIAG_PAGE_CONTROLS;
    cal_target_t cal_target = CAL_TARGET_JOYSTICK;
    cal_ui_state_t cal_state = CAL_UI_IDLE;
    TickType_t next_refresh = 0;
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    mic_diagnostic_t mic = {0};
    mic_diagnostic_reset(&mic, xTaskGetTickCount());
#endif

    while (true) {
        TickType_t now = xTaskGetTickCount();

        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            log_event(&event);
            if (event.type != CONTROL_EVENT_SWITCH_PRESSED) {
                continue;
            }

            if (event.sw == CONTROL_MODE_MENU) {
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
                page = page == DIAG_PAGE_CONTROLS ? DIAG_PAGE_MIC : DIAG_PAGE_CONTROLS;
                ESP_LOGI(TAG, "diagnostic page: %s", page == DIAG_PAGE_MIC ? "microphone" : "controls");
                if (page == DIAG_PAGE_MIC) {
                    mic_diagnostic_reset(&mic, now);
                    ESP_LOGI(MIC_TAG,
                             "raw stereo input meter started selected_capture_channel=%s sample_rate=%u",
                             ESP_POCKET_AUDIO_MIC_INPUT_CHANNEL == ESP_POCKET_AUDIO_INPUT_CHANNEL_RIGHT ?
                                 "right" : "left",
                             ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ);
                    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_dump_registers(&hw->codec, MIC_TAG));
                }
                continue;
#elif ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
                cal_target = cal_target == CAL_TARGET_JOYSTICK ? CAL_TARGET_WHEEL : CAL_TARGET_JOYSTICK;
                cal_state = CAL_UI_IDLE;
                ESP_LOGI(TAG, "calibration target: %s", cal_target_name(cal_target));
#endif
            }
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
            if (page == DIAG_PAGE_MIC) {
                continue;
            }
#endif
            if (event.sw == CONTROL_JOYSTICK_CLICK) {
                if (cal_target == CAL_TARGET_JOYSTICK) {
                    control_joystick_state_t joystick = {0};
                    controls_joystick_read(&joystick);
                    joystick_calibration_capture_center(joystick.x.raw, joystick.y.raw);
                    joystick_calibration_begin_range_capture(joystick.x.raw, joystick.y.raw);
                    cal_state = CAL_UI_CAPTURE_RANGE;
                }
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
                else {
                    control_wheel_state_t wheel = {0};
                    if (controls_wheel_read(&wheel) == ESP_OK && wheel.present) {
                        wheel_calibration_begin_range_capture(wheel.data.absolute.raw);
                        cal_state = CAL_UI_CAPTURE_RANGE;
                    } else {
                        cal_state = CAL_UI_ERROR;
                    }
                }
#endif
            } else if (event.sw == CONTROL_SOUND_MENU) {
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
                esp_err_t err = cal_target == CAL_TARGET_JOYSTICK ?
                                joystick_calibration_save() :
                                wheel_calibration_save();
#else
                esp_err_t err = joystick_calibration_save();
#endif
                cal_state = err == ESP_OK ? CAL_UI_SAVED : CAL_UI_ERROR;
                ESP_LOGI(TAG, "%s calibration save: %s", cal_target_name(cal_target), esp_err_to_name(err));
            } else if (event.sw == CONTROL_KEY_MENU) {
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
                esp_err_t err = cal_target == CAL_TARGET_JOYSTICK ?
                                joystick_calibration_reset() :
                                wheel_calibration_reset();
#else
                esp_err_t err = joystick_calibration_reset();
#endif
                cal_state = err == ESP_OK ? CAL_UI_RESET : CAL_UI_ERROR;
                ESP_LOGI(TAG, "%s calibration reset: %s", cal_target_name(cal_target), esp_err_to_name(err));
            }
        }

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        if (page == DIAG_PAGE_MIC) {
            mic_diagnostic_read(hw, &mic);
            mic_diagnostic_log(&mic, now);
        }
#endif

        if (page == DIAG_PAGE_CONTROLS && cal_state == CAL_UI_CAPTURE_RANGE) {
            if (cal_target == CAL_TARGET_JOYSTICK) {
                control_joystick_state_t joystick = {0};
                if (controls_joystick_read(&joystick) == ESP_OK) {
                    joystick_calibration_observe_range(joystick.x.raw, joystick.y.raw);
                }
            }
#if ESP_POCKET_AUDIO_WHEEL_SOURCE == ESP_POCKET_AUDIO_WHEEL_SOURCE_ADS1115
            else {
                control_wheel_state_t wheel = {0};
                if (controls_wheel_read(&wheel) == ESP_OK && wheel.present) {
                    wheel_calibration_observe_range(wheel.data.absolute.raw);
                }
            }
#endif
        }

        if ((int32_t)(now - next_refresh) >= 0) {
            draw_display(hw,
                         page,
                         cal_target,
                         cal_state
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
                         , &mic
#endif
                         );
            next_refresh = now + UI_REFRESH_TICKS;
        }

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
        vTaskDelay(page == DIAG_PAGE_MIC ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(20));
#else
        vTaskDelay(pdMS_TO_TICKS(20));
#endif
    }
}
