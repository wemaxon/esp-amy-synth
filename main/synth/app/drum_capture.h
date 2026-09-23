#ifndef SYNTH_APP_DRUM_CAPTURE_H
#define SYNTH_APP_DRUM_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/looper.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t tracked_recording;
    bool tracking;
} synth_app_drum_capture_t;

bool synth_app_drum_capture_update(synth_app_drum_capture_t *capture,
                                   const synth_looper_snapshot_t *snapshot,
                                   bool drum_generator_running);

#ifdef __cplusplus
}
#endif

#endif
