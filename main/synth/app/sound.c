#include "synth/app/sound.h"

#include <stddef.h>

synth_sound_t synth_app_sound_current(const synth_app_sound_t *sound)
{
    return sound != NULL ? sound->current : SYNTH_SOUND_SAW;
}

synth_sound_t synth_app_sound_pending(const synth_app_sound_t *sound)
{
    return sound != NULL ? sound->pending : synth_app_sound_current(sound);
}

bool synth_app_sound_has_pending(const synth_app_sound_t *sound)
{
    return sound != NULL && sound->apply_pending;
}

void synth_app_sound_mark_applied(synth_app_sound_t *sound, synth_sound_t selected)
{
    if (sound == NULL) {
        return;
    }
    sound->current = selected;
    sound->pending = selected;
    sound->apply_pending = false;
    sound->apply_due_tick = 0;
}

void synth_app_sound_schedule(synth_app_sound_t *sound,
                              synth_sound_t selected,
                              TickType_t now,
                              TickType_t delay_ticks)
{
    if (sound == NULL) {
        return;
    }
    sound->current = selected;
    sound->pending = selected;
    sound->apply_pending = true;
    sound->apply_due_tick = now + delay_ticks;
}

bool synth_app_sound_pending_due(const synth_app_sound_t *sound, TickType_t now)
{
    if (!synth_app_sound_has_pending(sound)) {
        return false;
    }
    return (int32_t)(now - sound->apply_due_tick) >= 0;
}
