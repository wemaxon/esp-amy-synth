#include "hichord_theory.h"

#include <stdbool.h>

static const uint8_t MAJOR_SCALE_INTERVALS[HICHORD_CHORD_BUTTON_COUNT] = {0, 2, 4, 5, 7, 9, 11};

typedef enum {
    DIATONIC_MAJOR = 0,
    DIATONIC_MINOR,
    DIATONIC_DIMINISHED,
} diatonic_quality_t;

static const diatonic_quality_t DIATONIC_QUALITIES[HICHORD_CHORD_BUTTON_COUNT] = {
    DIATONIC_MAJOR,
    DIATONIC_MINOR,
    DIATONIC_MINOR,
    DIATONIC_MAJOR,
    DIATONIC_MAJOR,
    DIATONIC_MINOR,
    DIATONIC_DIMINISHED,
};

const char *hichord_quality_name(hichord_quality_t quality)
{
    switch (quality) {
    case HICHORD_QUALITY_DIATONIC:
        return "DIATONIC";
    case HICHORD_QUALITY_TOGGLE_MAJOR_MINOR:
        return "MAJ_MIN";
    case HICHORD_QUALITY_SEVENTH:
        return "SEVENTH";
    case HICHORD_QUALITY_NINTH:
        return "NINTH";
    case HICHORD_QUALITY_SIXTH:
        return "SIXTH";
    case HICHORD_QUALITY_SUS2:
        return "SUS2";
    case HICHORD_QUALITY_SUS4:
        return "SUS4";
    case HICHORD_QUALITY_DIMINISHED:
        return "DIM";
    case HICHORD_QUALITY_AUGMENTED:
        return "AUG";
    default:
        return "UNKNOWN";
    }
}

hichord_quality_t hichord_quality_from_joystick(control_joystick_direction_t direction)
{
    switch (direction) {
    case CONTROL_JOYSTICK_CENTER:
        return HICHORD_QUALITY_DIATONIC;
    case CONTROL_JOYSTICK_UP:
        return HICHORD_QUALITY_SEVENTH;
    case CONTROL_JOYSTICK_UP_RIGHT:
        return HICHORD_QUALITY_NINTH;
    case CONTROL_JOYSTICK_RIGHT:
        return HICHORD_QUALITY_SUS4;
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        return HICHORD_QUALITY_AUGMENTED;
    case CONTROL_JOYSTICK_DOWN:
        return HICHORD_QUALITY_SIXTH;
    case CONTROL_JOYSTICK_DOWN_LEFT:
        return HICHORD_QUALITY_DIMINISHED;
    case CONTROL_JOYSTICK_LEFT:
        return HICHORD_QUALITY_SUS2;
    case CONTROL_JOYSTICK_UP_LEFT:
        return HICHORD_QUALITY_TOGGLE_MAJOR_MINOR;
    default:
        return HICHORD_QUALITY_DIATONIC;
    }
}

static bool degree_is_major(uint8_t degree)
{
    if (degree == 0 || degree > HICHORD_CHORD_BUTTON_COUNT) {
        return true;
    }
    return DIATONIC_QUALITIES[degree - 1] == DIATONIC_MAJOR;
}

static void add_intervals(hichord_chord_t *chord, uint8_t root, const uint8_t *intervals, uint8_t count)
{
    chord->note_count = count > HICHORD_MAX_CHORD_NOTES ? HICHORD_MAX_CHORD_NOTES : count;
    for (uint8_t i = 0; i < chord->note_count; ++i) {
        chord->notes[i] = root + intervals[i];
    }
}

int hichord_chord_for_degree(uint8_t key_root_midi, uint8_t degree, hichord_quality_t quality, hichord_chord_t *chord)
{
    if (degree == 0 || degree > HICHORD_CHORD_BUTTON_COUNT || chord == NULL) {
        return -1;
    }

    uint8_t root = key_root_midi + MAJOR_SCALE_INTERVALS[degree - 1];
    diatonic_quality_t diatonic = DIATONIC_QUALITIES[degree - 1];
    bool major_family = degree_is_major(degree);

    static const uint8_t MAJOR_TRIAD[] = {0, 4, 7};
    static const uint8_t MINOR_TRIAD[] = {0, 3, 7};
    static const uint8_t DIM_TRIAD[] = {0, 3, 6};
    static const uint8_t AUG_TRIAD[] = {0, 4, 8};
    static const uint8_t MAJOR_SEVENTH[] = {0, 4, 7, 11};
    static const uint8_t DOM_SEVENTH[] = {0, 4, 7, 10};
    static const uint8_t MINOR_SEVENTH[] = {0, 3, 7, 10};
    static const uint8_t DIM_SEVENTH[] = {0, 3, 6, 10};
    static const uint8_t MAJOR_NINTH[] = {0, 4, 7, 11, 14};
    static const uint8_t DOM_NINTH[] = {0, 4, 7, 10, 14};
    static const uint8_t MINOR_NINTH[] = {0, 3, 7, 10, 14};
    static const uint8_t SIXTH_MAJOR[] = {0, 4, 7, 9};
    static const uint8_t SIXTH_MINOR[] = {0, 3, 7, 9};
    static const uint8_t SUS2[] = {0, 2, 7};
    static const uint8_t SUS4[] = {0, 5, 7};

    chord->quality = quality;
    switch (quality) {
    case HICHORD_QUALITY_DIATONIC:
        if (diatonic == DIATONIC_MAJOR) {
            add_intervals(chord, root, MAJOR_TRIAD, sizeof(MAJOR_TRIAD));
        } else if (diatonic == DIATONIC_MINOR) {
            add_intervals(chord, root, MINOR_TRIAD, sizeof(MINOR_TRIAD));
        } else {
            add_intervals(chord, root, DIM_TRIAD, sizeof(DIM_TRIAD));
        }
        break;
    case HICHORD_QUALITY_TOGGLE_MAJOR_MINOR:
        if (major_family) {
            add_intervals(chord, root, MINOR_TRIAD, sizeof(MINOR_TRIAD));
        } else {
            add_intervals(chord, root, MAJOR_TRIAD, sizeof(MAJOR_TRIAD));
        }
        break;
    case HICHORD_QUALITY_SEVENTH:
        if (diatonic == DIATONIC_MAJOR && degree == 5) {
            add_intervals(chord, root, DOM_SEVENTH, sizeof(DOM_SEVENTH));
        } else if (diatonic == DIATONIC_MAJOR) {
            add_intervals(chord, root, MAJOR_SEVENTH, sizeof(MAJOR_SEVENTH));
        } else if (diatonic == DIATONIC_MINOR) {
            add_intervals(chord, root, MINOR_SEVENTH, sizeof(MINOR_SEVENTH));
        } else {
            add_intervals(chord, root, DIM_SEVENTH, sizeof(DIM_SEVENTH));
        }
        break;
    case HICHORD_QUALITY_NINTH:
        if (diatonic == DIATONIC_MAJOR && degree == 5) {
            add_intervals(chord, root, DOM_NINTH, sizeof(DOM_NINTH));
        } else if (diatonic == DIATONIC_MAJOR) {
            add_intervals(chord, root, MAJOR_NINTH, sizeof(MAJOR_NINTH));
        } else {
            add_intervals(chord, root, MINOR_NINTH, sizeof(MINOR_NINTH));
        }
        break;
    case HICHORD_QUALITY_SIXTH:
        add_intervals(chord, root, major_family ? SIXTH_MAJOR : SIXTH_MINOR, sizeof(SIXTH_MAJOR));
        break;
    case HICHORD_QUALITY_SUS2:
        add_intervals(chord, root, SUS2, sizeof(SUS2));
        break;
    case HICHORD_QUALITY_SUS4:
        add_intervals(chord, root, SUS4, sizeof(SUS4));
        break;
    case HICHORD_QUALITY_DIMINISHED:
        add_intervals(chord, root, DIM_TRIAD, sizeof(DIM_TRIAD));
        break;
    case HICHORD_QUALITY_AUGMENTED:
        add_intervals(chord, root, AUG_TRIAD, sizeof(AUG_TRIAD));
        break;
    default:
        return -1;
    }

    return 0;
}
