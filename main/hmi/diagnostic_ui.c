#include "hmi/diagnostic_ui.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/control_bus.h"
#include "hmi/controls.h"
#include "hmi/joystick_calibration.h"
#include "pocket_oled.h"

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
    chord[0] = (mask & (1UL << CONTROL_KEY_F)) ? '1' : '.';
    chord[1] = (mask & (1UL << CONTROL_KEY_F_SHARP)) ? '2' : '.';
    chord[2] = (mask & (1UL << CONTROL_KEY_G)) ? '3' : '.';
    chord[3] = (mask & (1UL << CONTROL_KEY_G_SHARP)) ? '4' : '.';
    chord[4] = (mask & (1UL << CONTROL_KEY_A)) ? '5' : '.';
    chord[5] = (mask & (1UL << CONTROL_KEY_A_SHARP)) ? '6' : '.';
    chord[6] = (mask & (1UL << CONTROL_KEY_H)) ? '7' : '.';
    chord[7] = '\0';

    char tempo = (mask & (1UL << CONTROL_KEY_TEMPO)) ? 'T' : '.';
    char mode = (mask & (1UL << CONTROL_KEY_MODE)) ? 'M' : '.';
    char sound = (mask & (1UL << CONTROL_KEY_SOUND)) ? 'S' : '.';
    char joy = (mask & (1UL << CONTROL_JOYSTICK_CLICK)) ? 'J' : '.';
    snprintf(line, line_size, "SW %s %c%c%c%c", chord, tempo, mode, sound, joy);
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

static void draw_display(const esp_pocket_audio_hw_t *hw, cal_ui_state_t state)
{
    if (hw == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    control_joystick_state_t joystick = {0};
    controls_joystick_read(&joystick);

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

    snprintf(line, sizeof(line), "CAL %s", cal_state_name(state));
    pocket_oled_draw_text_row(display, 5, line);
    pocket_oled_draw_text_row(display, 6, "J=center S=save");
    pocket_oled_draw_text_row(display, 7, "move stick T=reset");

    esp_err_t err = control_bus_lock(pdMS_TO_TICKS(250));
    if (err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(pocket_oled_flush(display));
        control_bus_unlock();
    } else {
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

            control_joystick_state_t joystick = {0};
            controls_joystick_read(&joystick);
            if (event.sw == CONTROL_JOYSTICK_CLICK) {
                joystick_calibration_capture_center(joystick.x.raw, joystick.y.raw);
                joystick_calibration_begin_range_capture(joystick.x.raw, joystick.y.raw);
                cal_state = CAL_UI_CAPTURE_RANGE;
            } else if (event.sw == CONTROL_KEY_SOUND) {
                esp_err_t err = joystick_calibration_save();
                cal_state = err == ESP_OK ? CAL_UI_SAVED : CAL_UI_ERROR;
                ESP_LOGI(TAG, "joystick calibration save: %s", esp_err_to_name(err));
            } else if (event.sw == CONTROL_KEY_TEMPO) {
                esp_err_t err = joystick_calibration_reset();
                cal_state = err == ESP_OK ? CAL_UI_RESET : CAL_UI_ERROR;
                ESP_LOGI(TAG, "joystick calibration reset: %s", esp_err_to_name(err));
            }
        }

        control_joystick_state_t joystick = {0};
        if (cal_state == CAL_UI_CAPTURE_RANGE && controls_joystick_read(&joystick) == ESP_OK) {
            joystick_calibration_observe_range(joystick.x.raw, joystick.y.raw);
        }

        if ((int32_t)(now - next_refresh) >= 0) {
            draw_display(hw, cal_state);
            next_refresh = now + UI_REFRESH_TICKS;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
