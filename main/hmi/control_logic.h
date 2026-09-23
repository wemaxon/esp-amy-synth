#ifndef HMI_CONTROL_LOGIC_H
#define HMI_CONTROL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hmi/controls.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_EVENT_QUEUE_CAPACITY 16

typedef struct {
    bool stable_pressed;
    bool sampled_pressed;
    TickType_t changed_at;
} control_debounce_t;

typedef enum {
    CONTROL_DEBOUNCE_UNCHANGED,
    CONTROL_DEBOUNCE_SAMPLED,
    CONTROL_DEBOUNCE_READY,
} control_debounce_result_t;

/* The caller publishes stable_pressed under its existing state lock. */
control_debounce_result_t control_debounce_sample(control_debounce_t *state,
                                                   bool pressed,
                                                   TickType_t now,
                                                   TickType_t delay);

typedef struct {
    control_event_t events[CONTROL_EVENT_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    uint32_t overwritten;
} control_event_queue_t;

void control_event_queue_reset(control_event_queue_t *queue);
void control_event_queue_push(control_event_queue_t *queue, control_switch_t sw, bool pressed);
esp_err_t control_event_queue_pop(control_event_queue_t *queue, control_event_t *event);

#ifdef __cplusplus
}
#endif

#endif

