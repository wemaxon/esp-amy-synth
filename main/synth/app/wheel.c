#include "synth/app/wheel.h"

#include <limits.h>
#include <stddef.h>

bool synth_app_wheel_update(synth_app_wheel_t *wheel, bool present, uint8_t percent)
{
    if (wheel == NULL) {
        return false;
    }

    bool changed = wheel->present != present || wheel->percent != percent;
    wheel->present = present;
    wheel->percent = percent;
    wheel->relative_position_valid = false;
    return changed;
}

bool synth_app_wheel_update_relative(synth_app_wheel_t *wheel,
                                     bool present,
                                     int32_t position,
                                     int32_t *delta_detents)
{
    if (wheel == NULL || delta_detents == NULL) {
        return false;
    }

    *delta_detents = 0;
    bool changed = wheel->present != present;
    wheel->present = present;
    if (!present) {
        wheel->relative_position_valid = false;
        return changed;
    }
    if (!wheel->relative_position_valid) {
        wheel->relative_position = position;
        wheel->relative_position_valid = true;
        return changed;
    }

    int64_t delta = (int64_t)position - wheel->relative_position;
    wheel->relative_position = position;
    if (delta > INT32_MAX) {
        delta = INT32_MAX;
    } else if (delta < INT32_MIN) {
        delta = INT32_MIN;
    }
    *delta_detents = (int32_t)delta;
    return changed || delta != 0;
}

bool synth_app_wheel_should_apply(synth_app_wheel_t *wheel,
                                  synth_app_wheel_target_t target,
                                  uint8_t percent,
                                  uint8_t threshold_percent)
{
    if (wheel == NULL) {
        return false;
    }

    bool changed = target != wheel->last_applied_target ||
                   wheel->last_applied_percent == UINT8_MAX;
    if (!changed) {
        uint8_t last = wheel->last_applied_percent;
        uint8_t delta = percent > last ? (uint8_t)(percent - last) : (uint8_t)(last - percent);
        changed = delta >= threshold_percent;
    }
    if (!changed) {
        return false;
    }

    wheel->last_applied_percent = percent;
    wheel->last_applied_target = target;
    return true;
}

uint8_t synth_app_wheel_adjust_percent(uint8_t current_percent,
                                       int32_t delta_detents,
                                       uint8_t step_percent)
{
    int64_t adjusted = current_percent > 100U ? 100 : current_percent;
    adjusted += (int64_t)delta_detents * step_percent;
    if (adjusted < 0) {
        return 0;
    }
    if (adjusted > 100) {
        return 100;
    }
    return (uint8_t)adjusted;
}

bool synth_app_wheel_present(const synth_app_wheel_t *wheel)
{
    return wheel != NULL && wheel->present;
}

uint8_t synth_app_wheel_percent(const synth_app_wheel_t *wheel)
{
    return wheel != NULL ? wheel->percent : 0;
}

uint8_t synth_app_wheel_volume_percent(const synth_app_wheel_t *wheel)
{
    return wheel != NULL ? wheel->volume_percent : 0;
}

void synth_app_wheel_set_volume_percent(synth_app_wheel_t *wheel, uint8_t percent)
{
    if (wheel != NULL) {
        wheel->volume_percent = percent;
    }
}
