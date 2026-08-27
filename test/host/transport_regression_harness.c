#include <stdint.h>

#include "synth/transport_timing.c"
#include "synth/app/control_route.c"
#include "synth/app/drum_capture.c"

int32_t host_vector_transport_bar_matches_step_grid(void)
{
    uint64_t step_boundary = synth_transport_step_offset(44100, 127, 16, 4);
    uint32_t bar_boundary = synth_transport_bar_span(44100, 127, 1);
    return step_boundary == bar_boundary;
}

int32_t host_vector_transport_frame_span(void)
{
    return (int32_t)synth_transport_bar_span(44100, 127, 1);
}

int32_t host_vector_transport_long_run(void)
{
    return (int32_t)synth_transport_step_offset(1000, 127, 160000, 4);
}

int32_t host_vector_sound_menu_owns_joystick(void)
{
    return (int32_t)synth_app_control_route(false,
                                            true,
                                            false,
                                            true,
                                            SYNTH_PLAY_MODE_DRUM_LOOPS,
                                            false);
}

int32_t host_vector_drum_mode_routes_to_drums(void)
{
    return (int32_t)synth_app_control_route(false,
                                            false,
                                            false,
                                            false,
                                            SYNTH_PLAY_MODE_AUTO_DRUM,
                                            false);
}

int32_t host_vector_capture_stops_generator(void)
{
    synth_app_drum_capture_t capture = {0};
    synth_looper_snapshot_t snapshot = {0};
    snapshot.track_state[2] = SYNTH_LOOPER_TRACK_RECORDING;
    if (synth_app_drum_capture_update(&capture, &snapshot, true)) {
        return 0;
    }
    snapshot.track_state[2] = SYNTH_LOOPER_TRACK_LOOPING;
    return synth_app_drum_capture_update(&capture, &snapshot, true);
}

int32_t host_vector_manual_capture_keeps_generator_state(void)
{
    synth_app_drum_capture_t capture = {0};
    synth_looper_snapshot_t snapshot = {0};
    snapshot.track_state[1] = SYNTH_LOOPER_TRACK_RECORDING;
    synth_app_drum_capture_update(&capture, &snapshot, false);
    snapshot.track_state[1] = SYNTH_LOOPER_TRACK_LOOPING;
    return synth_app_drum_capture_update(&capture, &snapshot, false);
}

int32_t host_vector_cancelled_capture_keeps_generator(void)
{
    synth_app_drum_capture_t capture = {0};
    synth_looper_snapshot_t snapshot = {0};
    snapshot.track_state[4] = SYNTH_LOOPER_TRACK_RECORDING;
    synth_app_drum_capture_update(&capture, &snapshot, true);
    snapshot.track_state[4] = SYNTH_LOOPER_TRACK_EMPTY;
    return synth_app_drum_capture_update(&capture, &snapshot, true);
}

int32_t host_vector_late_generator_capture(void)
{
    synth_app_drum_capture_t capture = {0};
    synth_looper_snapshot_t snapshot = {0};
    snapshot.track_state[3] = SYNTH_LOOPER_TRACK_RECORDING;
    synth_app_drum_capture_update(&capture, &snapshot, false);
    synth_app_drum_capture_update(&capture, &snapshot, true);
    snapshot.track_state[3] = SYNTH_LOOPER_TRACK_LOOPING;
    return synth_app_drum_capture_update(&capture, &snapshot, true);
}
