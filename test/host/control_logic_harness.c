#include <stdint.h>

#include "hmi/control_logic.c"

static inline __attribute__((always_inline)) void fill_overflowing_queue(control_event_queue_t *queue)
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
