#include <stdint.h>

#include "synth/app/rhythm.c"

int32_t host_vector_rhythm_delayed_poll(void)
{
    uint32_t step = 0;
    TickType_t deadline = synth_app_rhythm_advance_deadline(
        0, SYNTH_RHYTHM_RATE_SIXTEENTH, 120, &step, 0);
    deadline = synth_app_rhythm_advance_deadline(
        deadline, SYNTH_RHYTHM_RATE_SIXTEENTH, 120, &step, 140);
    deadline = synth_app_rhythm_advance_deadline(
        deadline, SYNTH_RHYTHM_RATE_SIXTEENTH, 120, &step, 400);
    return (int32_t)(deadline * 10U + step);
}

int32_t host_vector_rhythm_swing_phase(void)
{
    uint32_t step = 0;
    TickType_t deadline = synth_app_rhythm_advance_deadline(
        0, SYNTH_RHYTHM_RATE_SWING_EIGHTH, 120, &step, 0);
    deadline = synth_app_rhythm_advance_deadline(
        deadline, SYNTH_RHYTHM_RATE_SWING_EIGHTH, 120, &step, 340);
    return (int32_t)(deadline * 10U + step);
}

int32_t host_vector_rhythm_tick_wrap(void)
{
    uint32_t step = 0;
    return (int32_t)synth_app_rhythm_advance_deadline(
        UINT32_MAX - 50U,
        SYNTH_RHYTHM_RATE_QUARTER,
        120,
        &step,
        UINT32_MAX - 40U);
}

int32_t host_vector_rhythm_long_run(void)
{
    uint32_t step = 0;
    TickType_t deadline = 0;
    for (uint32_t i = 0; i < 4800U; ++i) {
        deadline = synth_app_rhythm_advance_deadline(
            deadline,
            SYNTH_RHYTHM_RATE_SIXTEENTH,
            120,
            &step,
            deadline);
    }
    return (int32_t)deadline;
}
