#include "diagnostics/diagnostic_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/control_i2c.h"
#include "hmi/controls.h"
#include "hmi/joystick_calibration.h"
#include "hmi/oled_display.h"
#include "hmi/wheel_calibration.h"
#include "pocket_oled.h"

typedef enum {
    CAL_TARGET_JOYSTICK = 0,
    CAL_TARGET_WHEEL,
} cal_target_t;

typedef enum {
    CAL_UI_IDLE = 0,
    CAL_UI_CAPTURE_RANGE,
    CAL_UI_SAVED,
    CAL_UI_RESET,
    CAL_UI_ERROR,
} cal_ui_state_t;

static const char *TAG = "DIAG_UI";
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
    case CAL_TARGET_WHEEL:
        return "WHL";
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

static void draw_display(const esp_pocket_audio_hw_t *hw, cal_target_t target, cal_ui_state_t state)
{
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

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
    snprintf(line, sizeof(line), "X %05d %+04d", joystick.x.raw, norm_x);
    pocket_oled_draw_text_row(display, 2, line);
    snprintf(line, sizeof(line), "Y %05d %+04d", joystick.y.raw, norm_y);
    pocket_oled_draw_text_row(display, 3, line);

    snprintf(line, sizeof(line), "DIR %.16s", controls_joystick_direction_name(joystick.direction));
    pocket_oled_draw_text_row(display, 4, line);

    snprintf(line, sizeof(line), "CAL %s %s", cal_target_name(target), cal_state_name(state));
    pocket_oled_draw_text_row(display, 5, line);
    if (wheel.present) {
        snprintf(line, sizeof(line), "WHL %05d %3u%%", wheel.raw, wheel.percent);
    } else {
        snprintf(line, sizeof(line), "WHL disabled");
    }
    pocket_oled_draw_text_row(display, 6, line);
    pocket_oled_draw_text_row(display, 7, target == CAL_TARGET_JOYSTICK ?
                              "M=whl J=ctr S/K" :
                              "M=joy J=rng S/K");

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

    cal_target_t cal_target = CAL_TARGET_JOYSTICK;
    cal_ui_state_t cal_state = CAL_UI_IDLE;
    TickType_t next_refresh = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();

        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            log_event(&event);
            if (event.type != CONTROL_EVENT_SWITCH_PRESSED) {
                continue;
            }

            if (event.sw == CONTROL_MODE_MENU) {
                cal_target = cal_target == CAL_TARGET_JOYSTICK ? CAL_TARGET_WHEEL : CAL_TARGET_JOYSTICK;
                cal_state = CAL_UI_IDLE;
                ESP_LOGI(TAG, "calibration target: %s", cal_target_name(cal_target));
            } else if (event.sw == CONTROL_JOYSTICK_CLICK) {
                if (cal_target == CAL_TARGET_JOYSTICK) {
                    control_joystick_state_t joystick = {0};
                    controls_joystick_read(&joystick);
                    joystick_calibration_capture_center(joystick.x.raw, joystick.y.raw);
                    joystick_calibration_begin_range_capture(joystick.x.raw, joystick.y.raw);
                    cal_state = CAL_UI_CAPTURE_RANGE;
                } else {
                    control_wheel_state_t wheel = {0};
                    if (controls_wheel_read(&wheel) == ESP_OK && wheel.present) {
                        wheel_calibration_begin_range_capture(wheel.raw);
                        cal_state = CAL_UI_CAPTURE_RANGE;
                    } else {
                        cal_state = CAL_UI_ERROR;
                    }
                }
            } else if (event.sw == CONTROL_SOUND_MENU) {
                esp_err_t err = cal_target == CAL_TARGET_JOYSTICK ?
                                joystick_calibration_save() :
                                wheel_calibration_save();
                cal_state = err == ESP_OK ? CAL_UI_SAVED : CAL_UI_ERROR;
                ESP_LOGI(TAG, "%s calibration save: %s", cal_target_name(cal_target), esp_err_to_name(err));
            } else if (event.sw == CONTROL_KEY_MENU) {
                esp_err_t err = cal_target == CAL_TARGET_JOYSTICK ?
                                joystick_calibration_reset() :
                                wheel_calibration_reset();
                cal_state = err == ESP_OK ? CAL_UI_RESET : CAL_UI_ERROR;
                ESP_LOGI(TAG, "%s calibration reset: %s", cal_target_name(cal_target), esp_err_to_name(err));
            }
        }

        if (cal_state == CAL_UI_CAPTURE_RANGE) {
            if (cal_target == CAL_TARGET_JOYSTICK) {
                control_joystick_state_t joystick = {0};
                if (controls_joystick_read(&joystick) == ESP_OK) {
                    joystick_calibration_observe_range(joystick.x.raw, joystick.y.raw);
                }
            } else {
                control_wheel_state_t wheel = {0};
                if (controls_wheel_read(&wheel) == ESP_OK && wheel.present) {
                    wheel_calibration_observe_range(wheel.raw);
                }
            }
        }

        if ((int32_t)(now - next_refresh) >= 0) {
            draw_display(hw, cal_target, cal_state);
            next_refresh = now + UI_REFRESH_TICKS;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
