#include "test_check.h"
#include <stdint.h>

#include "synth/looper_dsp.h"
#include "synth/looper_track.h"

/* Recording advances the grid only; audio is written by the looper take. */
int32_t host_vector_looper_grid_only(void)
{
    int16_t samples[4] = {9, 9, 9, 9};
    synth_looper_track_t track = {
        .samples = samples,
        .volume_percent = 100,
    };
    synth_looper_track_start_recording(&track, 4);
    synth_looper_track_advance_recording(&track, 4);
    synth_looper_track_advance_recording(&track, 4);
    return track.record_position == 2 && samples[0] == 9 && samples[1] == 9;
}

int32_t host_vector_looper_exact_wrap(void)
{
    int16_t samples[] = {1, 2, 3, 4};
    synth_looper_track_t track = {
        .samples = samples,
        .state = SYNTH_LOOPER_TRACK_LOOPING,
        .length_frames = 4,
        .volume_percent = 100,
    };
    int32_t result = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        int32_t mixed = 0;
        synth_looper_track_mix_playback(&track, true, &mixed);
        result = (result * 10) + mixed;
    }
    return result;
}

int32_t host_vector_looper_muted_advances(void)
{
    int16_t samples[] = {1, 2, 3, 4};
    synth_looper_track_t track = {
        .samples = samples,
        .state = SYNTH_LOOPER_TRACK_LOOPING,
        .length_frames = 4,
        .volume_percent = 100,
    };
    int32_t mixed = 0;
    synth_looper_track_mix_playback(&track, false, &mixed);
    synth_looper_track_mix_playback(&track, false, &mixed);
    synth_looper_track_mix_playback(&track, false, &mixed);
    return (int32_t)track.play_position;
}

int32_t host_vector_looper_target_completion(void)
{
    int16_t samples[8] = {0};
    synth_looper_track_t track = {
        .samples = samples,
        .volume_percent = 100,
    };
    synth_looper_track_start_recording(&track, 4);
    int32_t completed_at = 0;
    for (int32_t position = 1; position <= 8; ++position) {
        if (synth_looper_track_advance_recording(&track, 8)) {
            completed_at = position;
            break;
        }
    }
    return completed_at;
}

int main(void)
{
    CHECK_VECTOR(host_vector_looper_grid_only, 1);
    CHECK_VECTOR(host_vector_looper_exact_wrap, 12341234);
    CHECK_VECTOR(host_vector_looper_muted_advances, 3);
    CHECK_VECTOR(host_vector_looper_target_completion, 4);
    return 0;
}
