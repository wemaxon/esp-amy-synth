#include "test_check.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "hmi/control_logic.h"

int32_t host_vector_debounce_bounce_and_hold(void)
{
    control_debounce_t state = {0};
    if (control_debounce_sample(&state, true, 100, 20) != CONTROL_DEBOUNCE_SAMPLED ||
        control_debounce_sample(&state, false, 105, 20) != CONTROL_DEBOUNCE_SAMPLED ||
        control_debounce_sample(&state, true, 110, 20) != CONTROL_DEBOUNCE_SAMPLED ||
        control_debounce_sample(&state, true, 129, 20) != CONTROL_DEBOUNCE_UNCHANGED ||
        control_debounce_sample(&state, true, 130, 20) != CONTROL_DEBOUNCE_READY ||
        state.stable_pressed) {
        return 0;
    }
    state.stable_pressed = true; /* Production publishes this under its lock. */
    return control_debounce_sample(&state, true, 900, 20) == CONTROL_DEBOUNCE_UNCHANGED;
}

int32_t host_vector_debounce_release_wrap(void)
{
    control_debounce_t state = {.stable_pressed = true, .sampled_pressed = true};
    return control_debounce_sample(&state, false, UINT32_MAX - 9U, 20) == CONTROL_DEBOUNCE_SAMPLED &&
           control_debounce_sample(&state, false, 9, 20) == CONTROL_DEBOUNCE_UNCHANGED &&
           control_debounce_sample(&state, false, 10, 20) == CONTROL_DEBOUNCE_READY;
}

static void fill_overflowing_queue(control_event_queue_t *queue)
{
    control_event_queue_push(queue, CONTROL_CHORD_1, true);
    control_event_queue_push(queue, CONTROL_CHORD_2, true);
    control_event_queue_push(queue, CONTROL_CHORD_3, true);
    control_event_queue_push(queue, CONTROL_CHORD_4, true);
    control_event_queue_push(queue, CONTROL_CHORD_5, true);
    control_event_queue_push(queue, CONTROL_CHORD_6, true);
    control_event_queue_push(queue, CONTROL_CHORD_7, true);
    control_event_queue_push(queue, CONTROL_JOYSTICK_CLICK, true);
    control_event_queue_push(queue, CONTROL_KEY_MENU, true);
    control_event_queue_push(queue, CONTROL_MODE_MENU, true);
    control_event_queue_push(queue, CONTROL_SOUND_MENU, true);
    control_event_queue_push(queue, CONTROL_CHORD_1, true);
    control_event_queue_push(queue, CONTROL_CHORD_2, true);
    control_event_queue_push(queue, CONTROL_CHORD_3, true);
    control_event_queue_push(queue, CONTROL_CHORD_4, true);
    control_event_queue_push(queue, CONTROL_CHORD_5, true);
    control_event_queue_push(queue, CONTROL_CHORD_6, true);
}

int32_t host_vector_direction_center(void)
{
    return controls_joystick_direction_from_axes(0.4f, -0.4f);
}

int32_t host_vector_direction_up(void)
{
    return controls_joystick_direction_from_axes(0.1f, 0.9f);
}

int32_t host_vector_direction_down_left(void)
{
    return controls_joystick_direction_from_axes(-0.6f, -0.8f);
}

int32_t host_vector_direction_right_boundary(void)
{
    return controls_joystick_direction_from_axes(0.75f, 0.2f);
}

/* Center is a circle: equal travel activates every direction. */
int32_t host_vector_direction_diagonal_same_travel(void)
{
    /* r = 0.73 both ways; the old square zone left the diagonal at CENTER. */
    return controls_joystick_direction_from_axes(0.73f, 0.0f) == CONTROL_JOYSTICK_RIGHT &&
           controls_joystick_direction_from_axes(0.52f, 0.52f) == CONTROL_JOYSTICK_UP_RIGHT &&
           controls_joystick_direction_from_axes(-0.52f, -0.52f) == CONTROL_JOYSTICK_DOWN_LEFT &&
           controls_joystick_direction_from_axes(0.0f, -0.73f) == CONTROL_JOYSTICK_DOWN &&
           controls_joystick_direction_from_axes(0.6f, 0.0f) == CONTROL_JOYSTICK_CENTER &&
           controls_joystick_direction_from_axes(0.43f, 0.43f) == CONTROL_JOYSTICK_CENTER;
}

/* Eight equal 45-degree sectors centred on the directions. */
int32_t host_vector_direction_sectors(void)
{
    static const struct { float degrees; control_joystick_direction_t expected; } cases[] = {
        {0.0f, CONTROL_JOYSTICK_RIGHT}, {22.0f, CONTROL_JOYSTICK_RIGHT},
        {23.0f, CONTROL_JOYSTICK_UP_RIGHT}, {67.0f, CONTROL_JOYSTICK_UP_RIGHT},
        {68.0f, CONTROL_JOYSTICK_UP}, {112.0f, CONTROL_JOYSTICK_UP},
        {113.0f, CONTROL_JOYSTICK_UP_LEFT}, {180.0f, CONTROL_JOYSTICK_LEFT},
        {225.0f, CONTROL_JOYSTICK_DOWN_LEFT}, {270.0f, CONTROL_JOYSTICK_DOWN},
        {315.0f, CONTROL_JOYSTICK_DOWN_RIGHT}, {338.0f, CONTROL_JOYSTICK_RIGHT},
        {-23.0f, CONTROL_JOYSTICK_DOWN_RIGHT},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        float radians = cases[i].degrees * 3.14159265f / 180.0f;
        if (controls_joystick_direction_from_axes(0.9f * cosf(radians), 0.9f * sinf(radians)) !=
            cases[i].expected) {
            return 0;
        }
    }
    return 1;
}

/* Hysteresis: a held direction survives small wobble at ring and sector edges. */
int32_t host_vector_direction_hysteresis(void)
{
    float r = 0.9f;
    float a70 = 70.0f * 3.14159265f / 180.0f;
    float a80 = 80.0f * 3.14159265f / 180.0f;
    return /* 70 degrees: UP from rest, but a held UP_RIGHT is kept */
           controls_joystick_direction_update(CONTROL_JOYSTICK_CENTER, r * cosf(a70), r * sinf(a70)) ==
               CONTROL_JOYSTICK_UP &&
           controls_joystick_direction_update(CONTROL_JOYSTICK_UP_RIGHT, r * cosf(a70), r * sinf(a70)) ==
               CONTROL_JOYSTICK_UP_RIGHT &&
           /* beyond the hysteresis band the new sector wins */
           controls_joystick_direction_update(CONTROL_JOYSTICK_UP_RIGHT, r * cosf(a80), r * sinf(a80)) ==
               CONTROL_JOYSTICK_UP &&
           /* ring: 0.6 does not engage from rest but holds an active direction */
           controls_joystick_direction_update(CONTROL_JOYSTICK_CENTER, 0.6f, 0.0f) == CONTROL_JOYSTICK_CENTER &&
           controls_joystick_direction_update(CONTROL_JOYSTICK_RIGHT, 0.6f, 0.0f) == CONTROL_JOYSTICK_RIGHT &&
           controls_joystick_direction_update(CONTROL_JOYSTICK_RIGHT, 0.5f, 0.0f) == CONTROL_JOYSTICK_CENTER &&
           /* an inner-ring sector change from an active direction is allowed */
           controls_joystick_direction_update(CONTROL_JOYSTICK_RIGHT, 0.0f, 0.6f) == CONTROL_JOYSTICK_UP &&
           controls_joystick_direction_update(CONTROL_JOYSTICK_RIGHT, NAN, 0.9f) == CONTROL_JOYSTICK_CENTER;
}

int32_t host_vector_queue_overwritten(void)
{
    control_event_queue_t queue;
    control_event_queue_reset(&queue);
    fill_overflowing_queue(&queue);
    return queue.overwritten;
}

int32_t host_vector_queue_first_retained(void)
{
    control_event_queue_t queue;
    control_event_t event = {0};
    control_event_queue_reset(&queue);
    fill_overflowing_queue(&queue);
    control_event_queue_pop(&queue, &event);
    return event.sw;
}

int32_t host_vector_queue_empty_result(void)
{
    control_event_queue_t queue;
    control_event_t event = {0};
    control_event_queue_reset(&queue);
    return control_event_queue_pop(&queue, &event);
}

int main(void)
{
    CHECK_VECTOR(host_vector_debounce_bounce_and_hold, 1);
    CHECK_VECTOR(host_vector_debounce_release_wrap, 1);
    CHECK_VECTOR(host_vector_direction_center, 0);
    CHECK_VECTOR(host_vector_direction_up, 1);
    CHECK_VECTOR(host_vector_direction_down_left, 6);
    CHECK_VECTOR(host_vector_direction_right_boundary, 3);
    CHECK_VECTOR(host_vector_direction_diagonal_same_travel, 1);
    CHECK_VECTOR(host_vector_direction_sectors, 1);
    CHECK_VECTOR(host_vector_direction_hysteresis, 1);
    CHECK_VECTOR(host_vector_queue_overwritten, 2);
    CHECK_VECTOR(host_vector_queue_first_retained, 2);
    CHECK_VECTOR(host_vector_queue_empty_result, 261);
    return 0;
}
