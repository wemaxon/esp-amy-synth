#ifndef HMI_ROTARY_ENCODER_POSITION_H
#define HMI_ROTARY_ENCODER_POSITION_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t pending_counts;
    int32_t position;
} rotary_encoder_position_t;

static inline int32_t rotary_encoder_position_advance(rotary_encoder_position_t *state,
                                                      int32_t delta_counts,
                                                      uint8_t counts_per_detent,
                                                      int8_t direction)
{
    if (state == NULL || counts_per_detent == 0 ||
        (direction != -1 && direction != 1)) {
        return state != NULL ? state->position : 0;
    }

    state->pending_counts += delta_counts;
    int32_t detents = state->pending_counts / counts_per_detent;
    state->pending_counts -= detents * counts_per_detent;
    state->position += detents * direction;
    return state->position;
}

#endif
