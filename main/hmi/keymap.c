#include "hmi/keymap.h"

#include <stddef.h>

static const keymap_chord_key_t CHORD_KEYS[KEYMAP_CHORD_KEY_COUNT] = {
    {.sw = CONTROL_CHORD_1, .label = "1", .degree = 1},
    {.sw = CONTROL_CHORD_2, .label = "2", .degree = 2},
    {.sw = CONTROL_CHORD_3, .label = "3", .degree = 3},
    {.sw = CONTROL_CHORD_4, .label = "4", .degree = 4},
    {.sw = CONTROL_CHORD_5, .label = "5", .degree = 5},
    {.sw = CONTROL_CHORD_6, .label = "6", .degree = 6},
    {.sw = CONTROL_CHORD_7, .label = "7", .degree = 7},
};

const keymap_chord_key_t *keymap_chord_keys(void)
{
    return CHORD_KEYS;
}

bool keymap_switch_to_chord_degree(control_switch_t sw, uint8_t *degree)
{
    if (degree == NULL) {
        return false;
    }

    for (size_t i = 0; i < KEYMAP_CHORD_KEY_COUNT; ++i) {
        if (CHORD_KEYS[i].sw == sw) {
            *degree = CHORD_KEYS[i].degree;
            return true;
        }
    }
    return false;
}
