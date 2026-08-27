#include <stdint.h>

#include "synth/looper_dsp.c"
#include "synth/looper_track.c"

int32_t host_vector_looper_raw_capture(void)
{
    int16_t samples[4] = {0};
    synth_looper_track_t track = {
        .samples = samples,
        .volume_percent = 100,
    };
    synth_looper_track_start_recording(&track, 4);
    synth_looper_track_record_sample(&track, 1234, 4);
    synth_looper_track_record_sample(&track, -2345, 4);
    synth_looper_track_record_sample(&track, 3456, 4);
    synth_looper_track_record_sample(&track, 4321, 4);
    synth_looper_track_finish_recording(&track, 4);
    return samples[0] == 1234 && samples[1] == -2345 &&
           samples[2] == 3456 && samples[3] == 4321;
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
        if (synth_looper_track_record_sample(&track, (int16_t)position, 8)) {
            completed_at = position;
            break;
        }
    }
    return completed_at;
}
