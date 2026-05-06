#ifndef HMI_KEYMAP_H
#define HMI_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEYMAP_CHORD_KEY_COUNT 7

typedef struct {
    control_switch_t sw;
    const char *label;
    uint8_t degree;
} keymap_chord_key_t;

const keymap_chord_key_t *keymap_chord_keys(void);
bool keymap_switch_to_chord_degree(control_switch_t sw, uint8_t *degree);

#ifdef __cplusplus
}
#endif

#endif
