#include "test_check.h"
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
    CHECK_VECTOR(host_vector_queue_overwritten, 2);
    CHECK_VECTOR(host_vector_queue_first_retained, 2);
    CHECK_VECTOR(host_vector_queue_empty_result, 261);
    return 0;
}
