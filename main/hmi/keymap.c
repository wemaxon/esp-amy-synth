#include "hmi/keymap.h"

#include <stddef.h>

static const keymap_chord_key_t CHORD_KEYS[KEYMAP_CHORD_KEY_COUNT] = {
    {.sw = CONTROL_KEY_F, .label = "F", .degree = 1},
    {.sw = CONTROL_KEY_F_SHARP, .label = "F#", .degree = 2},
    {.sw = CONTROL_KEY_G, .label = "G", .degree = 3},
    {.sw = CONTROL_KEY_G_SHARP, .label = "G#", .degree = 4},
    {.sw = CONTROL_KEY_A, .label = "A", .degree = 5},
    {.sw = CONTROL_KEY_A_SHARP, .label = "A#", .degree = 6},
    {.sw = CONTROL_KEY_H, .label = "H", .degree = 7},
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
