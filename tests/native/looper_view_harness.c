#include "test_check.h"
#include <string.h>
#include "synth/looper_view.h"


int32_t host_vector_looper_view_recording(void)
{
    synth_looper_runtime_view_t view = {
        .track_state = {SYNTH_LOOPER_TRACK_RECORDING},
        .track_position_frames = {44100}, .track_length_frames = {88200},
        .track_volume_percent = {73}, .target_record_frames = 176400,
        .mute_mask = 1, .solo_mask = 1, .message_code = SYNTH_LOOPER_MESSAGE_RECORDING,
    };
    synth_looper_snapshot_t snapshot;
    synth_looper_view_to_snapshot(44100, &view, &snapshot);
    return snapshot.track_ms[0] == 1000 && snapshot.progress_ms == 1000 &&
           snapshot.progress_total_ms == 4000 && snapshot.track_volume_percent[0] == 73 &&
           snapshot.track_muted[0] && snapshot.track_soloed[0] &&
           snapshot.message[0] == 'R' && snapshot.message[3] == 0;
}
int32_t host_vector_looper_view_sync_wait(void)
{
    synth_looper_runtime_view_t view = {
        .track_state = {SYNTH_LOOPER_TRACK_LOOPING, SYNTH_LOOPER_TRACK_WAITING},
        .track_position_frames = {22050}, .master_length_frames = 88200,
        .current_track = 1, .current_track_armed = true, .waiting_for_sync = true,
    };
    synth_looper_snapshot_t snapshot;
    synth_looper_view_to_snapshot(44100, &view, &snapshot);
    return snapshot.progress_ms == 500 && snapshot.wait_ms == 1500 &&
           snapshot.progress_total_ms == 2000;
}
int32_t host_vector_looper_view_count_in(void)
{
    synth_looper_runtime_view_t view = {
        .track_state = {SYNTH_LOOPER_TRACK_WAITING},
        .current_track_armed = true, .global_frame = 22050,
        .record_start_frame = 88200, .count_in_beat_frames = 22050,
    };
    synth_looper_snapshot_t snapshot;
    synth_looper_view_to_snapshot(44100, &view, &snapshot);
    return snapshot.wait_ms == 1500 && snapshot.progress_ms == 500 &&
           snapshot.progress_total_ms == 2000;
}
int32_t host_vector_looper_view_zero_rate(void)
{
    synth_looper_runtime_view_t view = {.track_length_frames = {44100}};
    synth_looper_snapshot_t snapshot;
    synth_looper_view_to_snapshot(0, &view, &snapshot);
    return snapshot.track_ms[0] == 0 && snapshot.master_ms == 0;
}

int main(void)
{
    CHECK_VECTOR(host_vector_looper_view_recording, 1);
    CHECK_VECTOR(host_vector_looper_view_sync_wait, 1);
    CHECK_VECTOR(host_vector_looper_view_count_in, 1);
    CHECK_VECTOR(host_vector_looper_view_zero_rate, 1);
    return 0;
}
