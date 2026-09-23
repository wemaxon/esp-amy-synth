#include "synth/app/drum_capture.h"

#include <stddef.h>

bool synth_app_drum_capture_update(synth_app_drum_capture_t *capture,
                                   const synth_looper_snapshot_t *snapshot,
                                   bool drum_generator_running)
{
    if (capture == NULL || snapshot == NULL) {
        return false;
    }

    bool stop_generator = false;
    if (capture->tracking) {
        synth_looper_track_state_t state =
            snapshot->track_state[capture->tracked_recording];
        if (!drum_generator_running ||
            state == SYNTH_LOOPER_TRACK_EMPTY ||
            state == SYNTH_LOOPER_TRACK_OFF) {
            capture->tracking = false;
        } else if (state == SYNTH_LOOPER_TRACK_LOOPING) {
            capture->tracking = false;
            stop_generator = true;
        }
    }

    if (!stop_generator && !capture->tracking && drum_generator_running) {
        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (snapshot->track_state[track] == SYNTH_LOOPER_TRACK_RECORDING) {
                capture->tracked_recording = track;
                capture->tracking = true;
                break;
            }
        }
    }
    return stop_generator;
}
