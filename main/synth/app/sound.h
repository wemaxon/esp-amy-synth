#ifndef SYNTH_APP_SOUND_H
#define SYNTH_APP_SOUND_H

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "synth/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_SOUND_INITIALIZER(initial_sound) \
    { \
        .current = (initial_sound), \
        .pending = (initial_sound), \
    }

typedef struct {
    synth_sound_t current;
    synth_sound_t pending;
    bool apply_pending;
    TickType_t apply_due_tick;
} synth_app_sound_t;

synth_sound_t synth_app_sound_current(const synth_app_sound_t *sound);
synth_sound_t synth_app_sound_pending(const synth_app_sound_t *sound);
bool synth_app_sound_has_pending(const synth_app_sound_t *sound);
void synth_app_sound_mark_applied(synth_app_sound_t *sound, synth_sound_t selected);
void synth_app_sound_schedule(synth_app_sound_t *sound,
                              synth_sound_t selected,
                              TickType_t now,
                              TickType_t delay_ticks);
bool synth_app_sound_pending_due(const synth_app_sound_t *sound, TickType_t now);

#ifdef __cplusplus
}
#endif

#endif
