#include "synth/looper_track.h"

#include <stddef.h>

#include "synth/looper_dsp.h"

void synth_looper_track_reset(synth_looper_track_t *track, uint8_t default_volume_percent)
{
    if (track == NULL) {
        return;
    }

    int16_t *samples = track->samples;
    *track = (synth_looper_track_t){
        .samples = samples,
        .state = SYNTH_LOOPER_TRACK_EMPTY,
        .volume_percent = default_volume_percent,
    };
}

void synth_looper_track_start_recording(synth_looper_track_t *track, uint32_t target_frames)
{
    if (track == NULL) {
        return;
    }

    track->state = SYNTH_LOOPER_TRACK_RECORDING;
    track->record_armed = false;
    track->sync_to_master = false;
    track->length_frames = 0;
    track->play_position = 0;
    track->record_position = 0;
    track->record_start_frame = 0;
    track->target_record_frames = target_frames;
    track->count_in_beat_frames = 0;
}

bool synth_looper_track_record_sample(synth_looper_track_t *track,
                                      int16_t sample,
                                      uint32_t capacity_frames)
{
    if (track == NULL || track->state != SYNTH_LOOPER_TRACK_RECORDING ||
        track->samples == NULL || track->record_position >= capacity_frames) {
        return track != NULL && track->record_position >= capacity_frames;
    }

    track->samples[track->record_position++] = sample;
    return (track->target_record_frames > 0 &&
            track->record_position >= track->target_record_frames) ||
           track->record_position >= capacity_frames;
}

void synth_looper_track_finish_recording(synth_looper_track_t *track, uint32_t length_frames)
{
    if (track == NULL) {
        return;
    }

    track->state = SYNTH_LOOPER_TRACK_LOOPING;
    track->record_armed = false;
    track->sync_to_master = false;
    track->length_frames = length_frames;
    track->play_position = 0;
    track->record_position = 0;
    track->record_start_frame = 0;
    track->target_record_frames = 0;
    track->count_in_beat_frames = 0;
}

bool synth_looper_track_mix_playback(synth_looper_track_t *track,
                                     bool audible,
                                     int32_t *mixed_sample)
{
    if (track == NULL || mixed_sample == NULL ||
        track->state != SYNTH_LOOPER_TRACK_LOOPING ||
        track->samples == NULL || track->length_frames == 0) {
        return false;
    }

    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
    }
    uint32_t position = track->play_position;
    track->play_position = position + 1U;
    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
    }

    if (!audible || track->volume_percent == 0) {
        return false;
    }

    *mixed_sample += synth_looper_dsp_apply_percent_gain(track->samples[position],
                                                         track->volume_percent);
    return true;
}
