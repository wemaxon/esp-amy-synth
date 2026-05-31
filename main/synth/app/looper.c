#include "synth/app/looper.h"

#define LOOP_UI_REFRESH_TICKS pdMS_TO_TICKS(250)

static synth_app_looper_result_t looper_result(synth_app_looper_action_t action)
{
    synth_app_looper_result_t result = {
        .ui_dirty = true,
        .action = action,
        .status = "LOOPER",
    };
    return result;
}

bool synth_app_looper_visible(const synth_app_looper_t *looper)
{
    return looper != NULL && looper->visible;
}

void synth_app_looper_hide(synth_app_looper_t *looper)
{
    if (looper == NULL) {
        return;
    }
    looper->visible = false;
    looper->menu_held = false;
    looper->menu_clear_used = false;
}

void synth_app_looper_reset_direction(synth_app_looper_t *looper)
{
    if (looper != NULL) {
        looper->last_direction = CONTROL_JOYSTICK_CENTER;
    }
}

void synth_app_looper_begin_menu_hold(synth_app_looper_t *looper)
{
    if (looper == NULL) {
        return;
    }
    looper->menu_held = true;
    looper->menu_clear_used = false;
}

void synth_app_looper_mark_menu_clear_used(synth_app_looper_t *looper)
{
    if (looper != NULL) {
        looper->menu_clear_used = true;
    }
}

void synth_app_looper_finish_menu_hold(synth_app_looper_t *looper)
{
    if (looper == NULL || !looper->menu_held) {
        return;
    }
    if (!looper->menu_clear_used) {
        looper->visible = false;
    }
    looper->menu_held = false;
    looper->menu_clear_used = false;
}

bool synth_app_looper_periodic_due(synth_app_looper_t *looper, bool screen_visible, TickType_t now)
{
    if (looper == NULL || !screen_visible) {
        return false;
    }
    if ((int32_t)(now - looper->next_ui_tick) < 0) {
        return false;
    }
    looper->next_ui_tick = now + LOOP_UI_REFRESH_TICKS;
    return true;
}

synth_app_looper_result_t synth_app_looper_open(synth_app_looper_t *looper, TickType_t now)
{
    if (looper == NULL) {
        return (synth_app_looper_result_t){0};
    }
    looper->visible = true;
    looper->last_direction = CONTROL_JOYSTICK_CENTER;
    looper->next_ui_tick = now;
    return looper_result(SYNTH_APP_LOOPER_ACTION_NONE);
}

synth_app_looper_result_t synth_app_looper_click(synth_app_looper_t *looper, TickType_t now)
{
    if (looper == NULL) {
        return (synth_app_looper_result_t){0};
    }
    if (!looper->visible) {
        return synth_app_looper_open(looper, now);
    }
    return looper_result(SYNTH_APP_LOOPER_ACTION_CLICK);
}

synth_app_looper_result_t synth_app_looper_clear_all(void)
{
    synth_app_looper_result_t result = looper_result(SYNTH_APP_LOOPER_ACTION_CLEAR_ALL);
    result.status = "LOOPER CLR";
    return result;
}

synth_app_looper_result_t synth_app_looper_handle_joystick(synth_app_looper_t *looper,
                                                           control_joystick_direction_t direction,
                                                           bool current_track_waiting)
{
    if (looper == NULL || direction == looper->last_direction) {
        return (synth_app_looper_result_t){0};
    }
    looper->last_direction = direction;

    synth_app_looper_result_t result = looper_result(SYNTH_APP_LOOPER_ACTION_NONE);
    if (direction == CONTROL_JOYSTICK_CENTER) {
        result.status = NULL;
        return result;
    }

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        result.action = current_track_waiting ?
            SYNTH_APP_LOOPER_ACTION_ADJUST_BARS :
            SYNTH_APP_LOOPER_ACTION_SELECT_TRACK;
        result.action_steps = -1;
        break;
    case CONTROL_JOYSTICK_RIGHT:
        result.action = current_track_waiting ?
            SYNTH_APP_LOOPER_ACTION_ADJUST_BARS :
            SYNTH_APP_LOOPER_ACTION_SELECT_TRACK;
        result.action_steps = 1;
        break;
    case CONTROL_JOYSTICK_DOWN:
        result.action = SYNTH_APP_LOOPER_ACTION_TOGGLE_PAUSE;
        break;
    default:
        break;
    }
    return result;
}
