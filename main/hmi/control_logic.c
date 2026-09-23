#include "hmi/control_logic.h"

control_debounce_result_t control_debounce_sample(control_debounce_t *state,
                                                   bool pressed,
                                                   TickType_t now,
                                                   TickType_t delay)
{
    if (pressed != state->sampled_pressed) {
        state->sampled_pressed = pressed;
        state->changed_at = now;
        return CONTROL_DEBOUNCE_SAMPLED;
    }
    if (pressed != state->stable_pressed && (TickType_t)(now - state->changed_at) >= delay) {
        return CONTROL_DEBOUNCE_READY;
    }
    return CONTROL_DEBOUNCE_UNCHANGED;
}

static const float DIRECTION_AXIS_DEADZONE = 0.75f;
static const float DIRECTION_DIAGONAL_AXIS_MIN = 0.55f;

static float abs_axis(float value)
{
    return value < 0.0f ? -value : value;
}

control_joystick_direction_t controls_joystick_direction_from_axes(float x, float y)
{
    float abs_x = abs_axis(x);
    float abs_y = abs_axis(y);
    if (abs_x < DIRECTION_AXIS_DEADZONE && abs_y < DIRECTION_AXIS_DEADZONE) {
        return CONTROL_JOYSTICK_CENTER;
    }

    if (abs_x >= DIRECTION_DIAGONAL_AXIS_MIN && abs_y >= DIRECTION_DIAGONAL_AXIS_MIN) {
        if (x > 0.0f) {
            return y > 0.0f ? CONTROL_JOYSTICK_UP_RIGHT : CONTROL_JOYSTICK_DOWN_RIGHT;
        }
        return y > 0.0f ? CONTROL_JOYSTICK_UP_LEFT : CONTROL_JOYSTICK_DOWN_LEFT;
    }

    if (abs_x >= abs_y) {
        return x > 0.0f ? CONTROL_JOYSTICK_RIGHT : CONTROL_JOYSTICK_LEFT;
    }
    return y > 0.0f ? CONTROL_JOYSTICK_UP : CONTROL_JOYSTICK_DOWN;
}

void control_event_queue_reset(control_event_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    *queue = (control_event_queue_t){0};
}

void control_event_queue_push(control_event_queue_t *queue, control_switch_t sw, bool pressed)
{
    if (queue == NULL) {
        return;
    }

    size_t next_head = (queue->head + 1U) % CONTROL_EVENT_QUEUE_CAPACITY;
    if (next_head == queue->tail) {
        queue->tail = (queue->tail + 1U) % CONTROL_EVENT_QUEUE_CAPACITY;
        if (queue->overwritten < UINT32_MAX) {
            ++queue->overwritten;
        }
    }
    queue->events[queue->head].sw = sw;
    queue->events[queue->head].type =
        pressed ? CONTROL_EVENT_SWITCH_PRESSED : CONTROL_EVENT_SWITCH_RELEASED;
    queue->head = next_head;
}

esp_err_t control_event_queue_pop(control_event_queue_t *queue, control_event_t *event)
{
    if (queue == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (queue->tail == queue->head) {
        return ESP_ERR_NOT_FOUND;
    }

    *event = queue->events[queue->tail];
    queue->tail = (queue->tail + 1U) % CONTROL_EVENT_QUEUE_CAPACITY;
    return ESP_OK;
}
