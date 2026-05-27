#include "synth/app_key.h"

static bool octave_step(int8_t current, int8_t octaves, int8_t min_value, int8_t max_value, int8_t *next)
{
    if (next == NULL) {
        return false;
    }

    int value = (int)current + (int)octaves;
    if (value < min_value) {
        value = min_value;
    } else if (value > max_value) {
        value = max_value;
    }

    *next = (int8_t)value;
    return *next != current;
}

bool synth_app_key_step(synth_key_t current_key,
                        int8_t current_global_octave,
                        int8_t semitones,
                        synth_key_t *next_key,
                        int8_t *next_global_octave)
{
    if (next_key == NULL ||
        next_global_octave == NULL ||
        current_key >= SYNTH_KEY_COUNT) {
        return false;
    }

    int key_value = (int)current_key + (int)semitones;
    int octave_value = current_global_octave;
    while (key_value < 0) {
        key_value += SYNTH_KEY_COUNT;
        --octave_value;
    }
    while (key_value >= SYNTH_KEY_COUNT) {
        key_value -= SYNTH_KEY_COUNT;
        ++octave_value;
    }

    if (octave_value < SYNTH_APP_GLOBAL_OCTAVE_MIN ||
        octave_value > SYNTH_APP_GLOBAL_OCTAVE_MAX) {
        return false;
    }

    *next_key = (synth_key_t)key_value;
    *next_global_octave = (int8_t)octave_value;
    return *next_key != current_key || *next_global_octave != current_global_octave;
}

bool synth_app_global_octave_step(int8_t current, int8_t octaves, int8_t *next)
{
    return octave_step(current,
                       octaves,
                       SYNTH_APP_GLOBAL_OCTAVE_MIN,
                       SYNTH_APP_GLOBAL_OCTAVE_MAX,
                       next);
}

bool synth_app_button_octave_step(int8_t current, int8_t octaves, int8_t *next)
{
    return octave_step(current,
                       octaves,
                       SYNTH_APP_BUTTON_OCTAVE_MIN,
                       SYNTH_APP_BUTTON_OCTAVE_MAX,
                       next);
}
