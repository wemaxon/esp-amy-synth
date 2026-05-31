#include "synth/app/wheel.h"

#include <stddef.h>

bool synth_app_wheel_update(synth_app_wheel_t *wheel, bool present, uint8_t percent)
{
    if (wheel == NULL) {
        return false;
    }

    bool changed = wheel->present != present || wheel->percent != percent;
    wheel->present = present;
    wheel->percent = percent;
    return changed;
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
