#include "synth/app_rhythm.h"

#include <stdbool.h>
#include <string.h>

#include "esp_random.h"

static const uint8_t ARP_MAX_STEP_NOTES = 2;

uint32_t synth_app_rhythm_interval_ms(synth_rhythm_rate_t rate,
                                      uint16_t bpm,
                                      uint32_t step_index)
{
    if (bpm == 0) {
        bpm = 120;
    }

    float beat_ms = 60000.0f / (float)bpm;
    float interval_ms = beat_ms * 0.5f;

    switch (rate) {
    case SYNTH_RHYTHM_RATE_WHOLE:
        interval_ms = beat_ms * 4.0f;
        break;
    case SYNTH_RHYTHM_RATE_HALF:
        interval_ms = beat_ms * 2.0f;
        break;
    case SYNTH_RHYTHM_RATE_QUARTER:
        interval_ms = beat_ms;
        break;
    case SYNTH_RHYTHM_RATE_EIGHTH:
        interval_ms = beat_ms * 0.5f;
        break;
    case SYNTH_RHYTHM_RATE_SIXTEENTH:
        interval_ms = beat_ms * 0.25f;
        break;
    case SYNTH_RHYTHM_RATE_SIXTEENTH_TRIPLET:
        interval_ms = beat_ms / 6.0f;
        break;
    case SYNTH_RHYTHM_RATE_THIRTY_SECOND:
        interval_ms = beat_ms * 0.125f;
        break;
    case SYNTH_RHYTHM_RATE_SWING_EIGHTH:
        interval_ms = (step_index & 1U) == 0 ? beat_ms * (2.0f / 3.0f) : beat_ms * (1.0f / 3.0f);
        break;
    case SYNTH_RHYTHM_RATE_SWING_SIXTEENTH:
        interval_ms = (step_index & 1U) == 0 ? beat_ms * (1.0f / 3.0f) : beat_ms * (1.0f / 6.0f);
        break;
    default:
        break;
    }

    if (interval_ms < 20.0f) {
        interval_ms = 20.0f;
    }
    return (uint32_t)(interval_ms + 0.5f);
}

static void sort_notes(uint8_t *notes, uint8_t note_count)
{
    for (uint8_t i = 1; i < note_count; ++i) {
        uint8_t note = notes[i];
        uint8_t j = i;
        while (j > 0 && notes[j - 1] > note) {
            notes[j] = notes[j - 1];
            --j;
        }
        notes[j] = note;
    }
}

static uint8_t arp_pattern_note_index(synth_arp_pattern_t pattern,
                                      uint8_t note_count,
                                      uint32_t step_index)
{
    if (note_count <= 1) {
        return 0;
    }

    switch (pattern) {
    case SYNTH_ARP_PATTERN_DOWN:
        return (uint8_t)((note_count - 1U) - (step_index % note_count));
    case SYNTH_ARP_PATTERN_UP_DOWN: {
        uint8_t cycle = (uint8_t)((note_count * 2U) - 2U);
        uint8_t pos = (uint8_t)(step_index % cycle);
        return pos < note_count ? pos : (uint8_t)(cycle - pos);
    }
    case SYNTH_ARP_PATTERN_DOWN_UP: {
        uint8_t cycle = (uint8_t)((note_count * 2U) - 2U);
        uint8_t pos = (uint8_t)(step_index % cycle);
        uint8_t up_index = pos < note_count ? pos : (uint8_t)(cycle - pos);
        return (uint8_t)((note_count - 1U) - up_index);
    }
    case SYNTH_ARP_PATTERN_RANDOM:
        return (uint8_t)(esp_random() % note_count);
    case SYNTH_ARP_PATTERN_UP:
    case SYNTH_ARP_PATTERN_FINGERPICK:
    default:
        return (uint8_t)(step_index % note_count);
    }
}

static void build_fingerpick_step(const uint8_t *notes,
                                  uint8_t note_count,
                                  uint32_t step_index,
                                  synth_chord_t *step)
{
    if (note_count == 0 || step == NULL) {
        return;
    }

    uint8_t high = note_count > 2 ? 2 : (uint8_t)(note_count - 1);
    uint8_t alt = note_count > 3 ? 3 : high;
    uint8_t low = (step_index & 1U) != 0 && note_count > 1 ? 1 : 0;
    uint8_t top = (step_index & 2U) != 0 ? alt : high;

    step->notes[0] = notes[low];
    step->note_count = 1;
    if (top != low && step->note_count < ARP_MAX_STEP_NOTES) {
        step->notes[step->note_count++] = notes[top];
    }
}

void synth_app_append_step_notes(synth_chord_t *dest, const synth_chord_t *step)
{
    if (dest == NULL || step == NULL) {
        return;
    }

    for (uint8_t i = 0; i < step->note_count && dest->note_count < SYNTH_MAX_CHORD_NOTES; ++i) {
        bool duplicate = false;
        for (uint8_t j = 0; j < dest->note_count; ++j) {
            if (dest->notes[j] == step->notes[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            dest->notes[dest->note_count++] = step->notes[i];
        }
    }
}

void synth_app_build_arp_step_chord(synth_arp_pattern_t pattern,
                                    uint32_t step_index,
                                    const synth_chord_t *source,
                                    synth_chord_t *step)
{
    if (source == NULL || step == NULL || source->note_count == 0) {
        return;
    }

    uint8_t notes[SYNTH_MAX_CHORD_NOTES] = {0};
    uint8_t note_count = source->note_count;
    if (note_count > SYNTH_MAX_CHORD_NOTES) {
        note_count = SYNTH_MAX_CHORD_NOTES;
    }
    memcpy(notes, source->notes, note_count);
    sort_notes(notes, note_count);

    *step = *source;
    strlcpy(step->name, "ARP", sizeof(step->name));
    step->note_count = 0;

    if (pattern == SYNTH_ARP_PATTERN_FINGERPICK) {
        build_fingerpick_step(notes, note_count, step_index, step);
        return;
    }

    step->notes[0] = notes[arp_pattern_note_index(pattern, note_count, step_index)];
    step->note_count = 1;
}
