#include "test_check.h"
#include <stdint.h>

#include "hmi/rotary_encoder_position.h"
#include "synth/app/wheel.h"
#include "synth/app/settings.h"

int32_t host_vector_wheel_initial_relative_no_delta(void)
{
    synth_app_wheel_t wheel = SYNTH_APP_WHEEL_INITIALIZER(80);
    int32_t delta = 99;
    synth_app_wheel_update_relative(&wheel, true, 12, &delta);
    return delta;
}

int32_t host_vector_wheel_relative_accumulates(void)
{
    synth_app_wheel_t wheel = SYNTH_APP_WHEEL_INITIALIZER(80);
    int32_t delta = 0;
    synth_app_wheel_update_relative(&wheel, true, 10, &delta);
    synth_app_wheel_update_relative(&wheel, true, 14, &delta);
    return delta;
}

int32_t host_vector_wheel_relative_decreases(void)
{
    synth_app_wheel_t wheel = SYNTH_APP_WHEEL_INITIALIZER(80);
    int32_t delta = 0;
    synth_app_wheel_update_relative(&wheel, true, 10, &delta);
    synth_app_wheel_update_relative(&wheel, true, 7, &delta);
    return delta;
}

int32_t host_vector_wheel_target_switch_without_motion(void)
{
    synth_app_wheel_t wheel = SYNTH_APP_WHEEL_INITIALIZER(80);
    int32_t delta = 0;
    synth_app_wheel_update_relative(&wheel, true, 25, &delta);
    synth_app_wheel_update_relative(&wheel, true, 25, &delta);
    return delta;
}

int32_t host_vector_wheel_fixed_step(void)
{
    return synth_app_wheel_adjust_percent(40, 3, 2);
}

int32_t host_vector_wheel_lower_clamp(void)
{
    return synth_app_wheel_adjust_percent(1, -1, 2);
}

int32_t host_vector_wheel_upper_clamp(void)
{
    return synth_app_wheel_adjust_percent(99, 1, 2);
}

int32_t host_vector_wheel_absolute_target_parity(void)
{
    synth_app_wheel_t wheel = SYNTH_APP_WHEEL_INITIALIZER(80);
    bool volume = synth_app_wheel_should_apply(&wheel,
                                               SYNTH_APP_WHEEL_TARGET_VOLUME,
                                               40,
                                               2);
    bool below_threshold = synth_app_wheel_should_apply(&wheel,
                                                        SYNTH_APP_WHEEL_TARGET_VOLUME,
                                                        41,
                                                        2);
    bool threshold = synth_app_wheel_should_apply(&wheel,
                                                  SYNTH_APP_WHEEL_TARGET_VOLUME,
                                                  42,
                                                  2);
    bool target_change = synth_app_wheel_should_apply(&wheel,
                                                      SYNTH_APP_WHEEL_TARGET_ATTACK,
                                                      42,
                                                      2);
    return (volume ? 1000 : 0) + (below_threshold ? 100 : 0) +
           (threshold ? 10 : 0) + (target_change ? 1 : 0);
}

int32_t host_vector_wheel_attack_round_trip(void)
{
    return synth_app_attack_wheel_percent(synth_app_attack_ms_from_wheel(38));
}

int32_t host_vector_wheel_release_round_trip(void)
{
    return synth_app_release_wheel_percent(synth_app_release_ms_from_wheel(73));
}

int32_t host_vector_encoder_partial_detent(void)
{
    rotary_encoder_position_t position = {0};
    rotary_encoder_position_advance(&position, -1, 4, -1);
    rotary_encoder_position_advance(&position, -1, 4, -1);
    return rotary_encoder_position_advance(&position, -1, 4, -1);
}

int32_t host_vector_encoder_complete_detent(void)
{
    rotary_encoder_position_t position = {0};
    rotary_encoder_position_advance(&position, -3, 4, -1);
    return rotary_encoder_position_advance(&position, -1, 4, -1);
}

int32_t host_vector_encoder_reverse_partial(void)
{
    rotary_encoder_position_t position = {0};
    rotary_encoder_position_advance(&position, -4, 4, -1);
    return rotary_encoder_position_advance(&position, 1, 4, -1);
}

int32_t host_vector_encoder_complete_reverse(void)
{
    rotary_encoder_position_t position = {0};
    rotary_encoder_position_advance(&position, -4, 4, -1);
    rotary_encoder_position_advance(&position, 1, 4, -1);
    return rotary_encoder_position_advance(&position, 3, 4, -1);
}

int main(void)
{
    CHECK_VECTOR(host_vector_wheel_initial_relative_no_delta, 0);
    CHECK_VECTOR(host_vector_wheel_relative_accumulates, 4);
    CHECK_VECTOR(host_vector_wheel_relative_decreases, -3);
    CHECK_VECTOR(host_vector_wheel_target_switch_without_motion, 0);
    CHECK_VECTOR(host_vector_wheel_fixed_step, 46);
    CHECK_VECTOR(host_vector_wheel_lower_clamp, 0);
    CHECK_VECTOR(host_vector_wheel_upper_clamp, 100);
    CHECK_VECTOR(host_vector_wheel_absolute_target_parity, 1011);
    CHECK_VECTOR(host_vector_wheel_attack_round_trip, 38);
    CHECK_VECTOR(host_vector_wheel_release_round_trip, 73);
    CHECK_VECTOR(host_vector_encoder_partial_detent, 0);
    CHECK_VECTOR(host_vector_encoder_complete_detent, 1);
    CHECK_VECTOR(host_vector_encoder_reverse_partial, 1);
    CHECK_VECTOR(host_vector_encoder_complete_reverse, 0);
    return 0;
}
