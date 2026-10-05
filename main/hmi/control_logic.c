#include "hmi/control_logic.h"

#include <math.h>

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

/* The eight modifiers sit on a circle around the center, so the center is a
 * circle too and each direction owns an equal 45-degree sector. A direction
 * engages at ENTER radius and holds down to EXIT radius; a held direction also
 * extends its sector by the hysteresis angle so edge wobble cannot flap it. */
static const float DIRECTION_ENTER_RADIUS = 0.70f;
static const float DIRECTION_EXIT_RADIUS = 0.55f;
static const float DIRECTION_SECTOR_HALF_DEG = 22.5f;
static const float DIRECTION_HYSTERESIS_DEG = 8.0f;
static const float RAD_TO_DEG = 57.2957795f;

/* Counter-clockwise from +x (right); up is +y. */
static const control_joystick_direction_t SECTOR_DIRECTIONS[8] = {
    CONTROL_JOYSTICK_RIGHT, CONTROL_JOYSTICK_UP_RIGHT, CONTROL_JOYSTICK_UP, CONTROL_JOYSTICK_UP_LEFT,
    CONTROL_JOYSTICK_LEFT, CONTROL_JOYSTICK_DOWN_LEFT, CONTROL_JOYSTICK_DOWN, CONTROL_JOYSTICK_DOWN_RIGHT,
};

static float direction_angle_deg(control_joystick_direction_t direction)
{
    for (unsigned sector = 0; sector < 8; ++sector) {
        if (SECTOR_DIRECTIONS[sector] == direction) {
            return (float)sector * 45.0f;
        }
    }
    return NAN;
}

control_joystick_direction_t controls_joystick_direction_update(control_joystick_direction_t previous,
                                                                float x,
                                                                float y)
{
    float radius = sqrtf(x * x + y * y);
    bool held = previous != CONTROL_JOYSTICK_CENTER;
    if (!(radius >= (held ? DIRECTION_EXIT_RADIUS : DIRECTION_ENTER_RADIUS))) {
        return CONTROL_JOYSTICK_CENTER;
    }

    float angle = atan2f(y, x) * RAD_TO_DEG;
    if (angle < 0.0f) {
        angle += 360.0f;
    }
    if (held) {
        float distance = fabsf(remainderf(angle - direction_angle_deg(previous), 360.0f));
        if (distance <= DIRECTION_SECTOR_HALF_DEG + DIRECTION_HYSTERESIS_DEG) {
            return previous;
        }
    }
    unsigned sector = (unsigned)floorf((angle + DIRECTION_SECTOR_HALF_DEG) / 45.0f) % 8U;
    return SECTOR_DIRECTIONS[sector];
}

control_joystick_direction_t controls_joystick_direction_from_axes(float x, float y)
{
    return controls_joystick_direction_update(CONTROL_JOYSTICK_CENTER, x, y);
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
