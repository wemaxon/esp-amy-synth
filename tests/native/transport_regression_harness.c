#include "test_check.h"
#include <stdint.h>

#include "synth/transport_timing.h"
#include "synth/app/control_route.h"

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

int main(void)
{
    CHECK_VECTOR(host_vector_transport_bar_matches_step_grid, 1);
    CHECK_VECTOR(host_vector_transport_frame_span, 83338);
    CHECK_VECTOR(host_vector_transport_long_run, 18897637);
    CHECK_VECTOR(host_vector_sound_menu_owns_joystick, 2);
    CHECK_VECTOR(host_vector_drum_mode_routes_to_drums, 6);
    return 0;
}
