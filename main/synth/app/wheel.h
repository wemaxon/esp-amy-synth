#ifndef SYNTH_APP_WHEEL_H
#define SYNTH_APP_WHEEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_APP_WHEEL_TARGET_NONE = 0,
    SYNTH_APP_WHEEL_TARGET_VOLUME,
    SYNTH_APP_WHEEL_TARGET_ATTACK,
    SYNTH_APP_WHEEL_TARGET_RELEASE,
    SYNTH_APP_WHEEL_TARGET_FILTER,
    SYNTH_APP_WHEEL_TARGET_LOOPER_TRACK,
} synth_app_wheel_target_t;

#define SYNTH_APP_WHEEL_INITIALIZER \
    { \
        .percent = 100, \
        .volume_percent = 100, \
        .last_applied_percent = UINT8_MAX, \
        .last_applied_target = SYNTH_APP_WHEEL_TARGET_NONE, \
    }

typedef struct {
    bool present;
    uint8_t percent;
    uint8_t volume_percent;
    uint8_t last_applied_percent;
    synth_app_wheel_target_t last_applied_target;
} synth_app_wheel_t;

bool synth_app_wheel_update(synth_app_wheel_t *wheel, bool present, uint8_t percent);
bool synth_app_wheel_should_apply(synth_app_wheel_t *wheel,
                                  synth_app_wheel_target_t target,
                                  uint8_t percent,
                                  uint8_t threshold_percent);
bool synth_app_wheel_present(const synth_app_wheel_t *wheel);
uint8_t synth_app_wheel_percent(const synth_app_wheel_t *wheel);
uint8_t synth_app_wheel_volume_percent(const synth_app_wheel_t *wheel);
void synth_app_wheel_set_volume_percent(synth_app_wheel_t *wheel, uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
