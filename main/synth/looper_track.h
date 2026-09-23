#ifndef SYNTH_LOOPER_TRACK_H
#define SYNTH_LOOPER_TRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "synth/looper.h"

typedef struct {
    int16_t *samples;
    synth_looper_track_state_t state;
    bool record_armed;
    bool sync_to_master;
    uint32_t length_frames;
    uint32_t play_position;
    uint32_t record_position;
    uint64_t record_start_frame;
    uint32_t target_record_frames;
    uint32_t count_in_beat_frames;
    uint8_t volume_percent;
} synth_looper_track_t;

void synth_looper_track_reset(synth_looper_track_t *track, uint8_t default_volume_percent);
void synth_looper_track_start_recording(synth_looper_track_t *track, uint32_t target_frames);
bool synth_looper_track_record_sample(synth_looper_track_t *track,
                                      int16_t sample,
                                      uint32_t capacity_frames);
void synth_looper_track_finish_recording(synth_looper_track_t *track, uint32_t length_frames);
bool synth_looper_track_mix_playback(synth_looper_track_t *track,
                                     bool audible,
                                     int32_t *mixed_sample);

#endif
