#include "synth/app/mixer.h"

#include <stddef.h>

#include "synth/looper.h"

static synth_app_mixer_result_t mixer_result(bool handled)
{
    synth_app_mixer_result_t result = {
        .handled = handled,
        .action = SYNTH_APP_MIXER_ACTION_NONE,
        .action_track = SYNTH_APP_MIXER_NO_TRACK,
        .focus_track = SYNTH_APP_MIXER_NO_TRACK,
    };
    return result;
}

void synth_app_mixer_reset(synth_app_mixer_t *mixer)
{
    if (mixer == NULL) {
        return;
    }
    mixer->held_mask = 0;
    mixer->action_mask = 0;
    mixer->wheel_track = SYNTH_APP_MIXER_NO_TRACK;
    mixer->wheel_start_percent = 100;
}

uint8_t synth_app_mixer_first_held_track(const synth_app_mixer_t *mixer)
{
    if (mixer == NULL) {
        return SYNTH_APP_MIXER_NO_TRACK;
    }

    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        if ((mixer->held_mask & (1U << track)) != 0) {
            return track;
        }
    }
    return SYNTH_APP_MIXER_NO_TRACK;
}

void synth_app_mixer_mark_action(synth_app_mixer_t *mixer, uint8_t track)
{
    if (mixer != NULL && track < SYNTH_LOOPER_TRACK_COUNT) {
        mixer->action_mask |= (uint8_t)(1U << track);
    }
}

bool synth_app_mixer_wheel_moved(synth_app_mixer_t *mixer,
                                 uint8_t track,
                                 uint8_t percent,
                                 uint8_t threshold_percent)
{
    if (mixer == NULL || track >= SYNTH_LOOPER_TRACK_COUNT) {
        return false;
    }
    if (mixer->wheel_track != track) {
        mixer->wheel_track = track;
        mixer->wheel_start_percent = percent;
        return false;
    }
    if ((mixer->action_mask & (1U << track)) != 0) {
        return true;
    }

    uint8_t start = mixer->wheel_start_percent;
    uint8_t delta = percent > start ? (uint8_t)(percent - start) : (uint8_t)(start - percent);
    return delta >= threshold_percent;
}

synth_app_mixer_result_t synth_app_mixer_handle_chord_event(synth_app_mixer_t *mixer,
                                                            const control_event_t *event,
                                                            uint8_t degree,
                                                            uint8_t wheel_percent)
{
    if (mixer == NULL || event == NULL) {
        return mixer_result(false);
    }

    if (degree == 7) {
        synth_app_mixer_result_t result = mixer_result(true);
        if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
            result.action = SYNTH_APP_MIXER_ACTION_TOGGLE_METRONOME;
            result.status = "METRONOME";
            result.ui_dirty = true;
        }
        return result;
    }
    if (degree == 0 || degree > SYNTH_LOOPER_TRACK_COUNT) {
        return mixer_result(true);
    }

    uint8_t track = (uint8_t)(degree - 1U);
    uint8_t bit = (uint8_t)(1U << track);
    if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
        mixer->held_mask |= bit;
        mixer->action_mask &= (uint8_t)~bit;
        mixer->wheel_track = track;
        mixer->wheel_start_percent = wheel_percent;

        synth_app_mixer_result_t result = mixer_result(true);
        result.focus_track = track;
        result.status = "MIXER";
        result.ui_dirty = true;
        return result;
    }

    synth_app_mixer_result_t result = mixer_result(true);
    if ((mixer->held_mask & bit) != 0 && (mixer->action_mask & bit) == 0) {
        result.action = SYNTH_APP_MIXER_ACTION_TOGGLE_MUTE;
        result.action_track = track;
        result.status = "MIX MUTE";
    }
    mixer->held_mask &= (uint8_t)~bit;
    mixer->action_mask &= (uint8_t)~bit;
    if (mixer->wheel_track == track) {
        mixer->wheel_track = SYNTH_APP_MIXER_NO_TRACK;
    }
    result.ui_dirty = true;
    return result;
}

synth_app_mixer_result_t synth_app_mixer_handle_joystick_click(synth_app_mixer_t *mixer)
{
    synth_app_mixer_result_t result = mixer_result(true);
    uint8_t track = synth_app_mixer_first_held_track(mixer);
    if (track < SYNTH_LOOPER_TRACK_COUNT) {
        synth_app_mixer_mark_action(mixer, track);
        result.action = SYNTH_APP_MIXER_ACTION_TOGGLE_SOLO;
        result.action_track = track;
        result.status = "SOLO";
    } else {
        result.action = SYNTH_APP_MIXER_ACTION_TOGGLE_PAUSE;
        result.status = "MIX PAUSE";
    }
    result.ui_dirty = true;
    return result;
}
