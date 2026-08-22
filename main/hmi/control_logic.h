#ifndef HMI_CONTROL_LOGIC_H
#define HMI_CONTROL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_EVENT_QUEUE_CAPACITY 16

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

