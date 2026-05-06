#ifndef HICHORD_THEORY_H
#define HICHORD_THEORY_H

#include <stddef.h>
#include <stdint.h>

#include "hmi/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HICHORD_CHORD_BUTTON_COUNT 7
#define HICHORD_MAX_CHORD_NOTES    8

typedef enum {
    HICHORD_QUALITY_DIATONIC = 0,
    HICHORD_QUALITY_TOGGLE_MAJOR_MINOR,
    HICHORD_QUALITY_SEVENTH,
    HICHORD_QUALITY_NINTH,
    HICHORD_QUALITY_SIXTH,
    HICHORD_QUALITY_SUS2,
    HICHORD_QUALITY_SUS4,
    HICHORD_QUALITY_DIMINISHED,
    HICHORD_QUALITY_AUGMENTED,
} hichord_quality_t;

typedef struct {
    uint8_t notes[HICHORD_MAX_CHORD_NOTES];
    uint8_t note_count;
    hichord_quality_t quality;
} hichord_chord_t;

const char *hichord_quality_name(hichord_quality_t quality);
hichord_quality_t hichord_quality_from_joystick(control_joystick_direction_t direction);
int hichord_chord_for_degree(uint8_t key_root_midi, uint8_t degree, hichord_quality_t quality, hichord_chord_t *chord);

#ifdef __cplusplus
}
#endif

#endif
