#ifndef SYNTH_APP_LOOPER_H
#define SYNTH_APP_LOOPER_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_LOOPER_INITIALIZER \
    { \
        .last_direction = CONTROL_JOYSTICK_CENTER, \
    }

typedef enum {
    SYNTH_APP_LOOPER_ACTION_NONE = 0,
    SYNTH_APP_LOOPER_ACTION_CLICK,
    SYNTH_APP_LOOPER_ACTION_CLEAR_ALL,
    SYNTH_APP_LOOPER_ACTION_SELECT_TRACK,
    SYNTH_APP_LOOPER_ACTION_ADJUST_BARS,
    SYNTH_APP_LOOPER_ACTION_TOGGLE_PAUSE,
} synth_app_looper_action_t;

typedef struct {
    bool visible;
    bool menu_held;
    bool menu_clear_used;
    TickType_t next_ui_tick;
    control_joystick_direction_t last_direction;
} synth_app_looper_t;

typedef struct {
    bool ui_dirty;
    synth_app_looper_action_t action;
    int8_t action_steps;
    const char *status;
} synth_app_looper_result_t;

bool synth_app_looper_visible(const synth_app_looper_t *looper);
void synth_app_looper_hide(synth_app_looper_t *looper);
void synth_app_looper_reset_direction(synth_app_looper_t *looper);
void synth_app_looper_begin_menu_hold(synth_app_looper_t *looper);
void synth_app_looper_mark_menu_clear_used(synth_app_looper_t *looper);
void synth_app_looper_finish_menu_hold(synth_app_looper_t *looper);
bool synth_app_looper_periodic_due(synth_app_looper_t *looper, bool screen_visible, TickType_t now);
synth_app_looper_result_t synth_app_looper_open(synth_app_looper_t *looper, TickType_t now);
synth_app_looper_result_t synth_app_looper_click(synth_app_looper_t *looper, TickType_t now);
synth_app_looper_result_t synth_app_looper_clear_all(void);
synth_app_looper_result_t synth_app_looper_handle_joystick(synth_app_looper_t *looper,
                                                           control_joystick_direction_t direction,
                                                           bool current_track_waiting);

#ifdef __cplusplus
}
#endif

#endif
