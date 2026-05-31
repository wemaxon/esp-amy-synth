#include "synth/app/chord_tools.h"

#include <stdio.h>
#include <string.h>

static int midi_note_octave(uint8_t midi_note)
{
    return ((int)midi_note / 12) - 1;
}

static uint8_t held_chord_count(const bool held_valid[SYNTH_CHORD_BUTTON_COUNT],
                                uint8_t held_mask)
{
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((held_mask & (1U << slot)) != 0 && held_valid[slot]) {
            ++count;
        }
    }
    return count;
}

static void append_unique_layer_note(synth_chord_t *layer, uint8_t note)
{
    if (layer == NULL || layer->note_count >= SYNTH_MAX_CHORD_NOTES) {
        return;
    }

    for (uint8_t i = 0; i < layer->note_count; ++i) {
        if (layer->notes[i] == note) {
            return;
        }
    }
    layer->notes[layer->note_count++] = note;
}

static uint8_t slot_from_degree(uint8_t degree)
{
    return degree > 0 ? (uint8_t)(degree - 1) : 0;
}

static synth_chord_modifier_t effective_modifier_for_slot(const synth_app_chord_build_context_t *context,
                                                          uint8_t slot)
{
    if (context == NULL) {
        return SYNTH_CHORD_MOD_NONE;
    }
    if (context->modifier != SYNTH_CHORD_MOD_NONE &&
        slot == context->focus_slot &&
        (context->held_mask & (1U << slot)) != 0) {
        return context->modifier;
    }
    if (slot < SYNTH_CHORD_BUTTON_COUNT && context->locked_modifier != NULL) {
        return context->locked_modifier[slot];
    }
    return SYNTH_CHORD_MOD_NONE;
}

uint8_t synth_app_first_held_slot_except(uint8_t held_mask, uint8_t excluded_slot)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if (slot != excluded_slot && (held_mask & (1U << slot)) != 0) {
            return slot;
        }
    }
    return UINT8_MAX;
}

uint8_t synth_app_bass_slot_for_chord(uint8_t chord_slot,
                                      uint8_t held_mask,
                                      synth_bass_mode_t bass_mode,
                                      uint8_t slash_bass_slot)
{
    if (bass_mode != SYNTH_BASS_MODE_SLASH) {
        return chord_slot;
    }
    if (slash_bass_slot < SYNTH_CHORD_BUTTON_COUNT &&
        slash_bass_slot != chord_slot &&
        (held_mask & (1U << slash_bass_slot)) != 0) {
        return slash_bass_slot;
    }

    uint8_t held_slot = synth_app_first_held_slot_except(held_mask, chord_slot);
    return held_slot < SYNTH_CHORD_BUTTON_COUNT ? held_slot : chord_slot;
}

bool synth_app_build_chord_for_degree(const synth_app_chord_build_context_t *context,
                                      uint8_t degree,
                                      synth_chord_t *chord)
{
    if (context == NULL ||
        context->button_octave_shift == NULL ||
        context->chord_inversion == NULL ||
        chord == NULL) {
        return false;
    }

    uint8_t slot = slot_from_degree(degree);
    if (slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return false;
    }

    uint8_t bass_slot = synth_app_bass_slot_for_chord(slot,
                                                      context->held_mask,
                                                      context->bass_mode,
                                                      context->slash_bass_slot);
    if (bass_slot >= SYNTH_CHORD_BUTTON_COUNT) {
        bass_slot = slot;
    }

    synth_chord_request_t request = {
        .key = context->key,
        .degree = degree,
        .global_octave_shift = context->global_octave_shift,
        .button_octave_shift = context->button_octave_shift[slot],
        .modifier = effective_modifier_for_slot(context, slot),
        .bass_mode = context->bass_mode,
        .inversion = context->chord_inversion[slot],
        .bass_degree = (uint8_t)(bass_slot + 1U),
        .bass_octave_shift = context->button_octave_shift[bass_slot],
        .voice_leading_enabled = context->voice_leading_enabled,
    };
    if (context->voice_leading_enabled &&
        context->voice_anchor_valid &&
        context->voice_anchor != NULL) {
        request.previous_note_count = context->voice_anchor->note_count;
        if (request.previous_note_count > SYNTH_MAX_CHORD_NOTES) {
            request.previous_note_count = SYNTH_MAX_CHORD_NOTES;
        }
        memcpy(request.previous_notes, context->voice_anchor->notes, request.previous_note_count);
    }
    return synth_build_chord(&request, chord) == 0;
}

void synth_app_format_chord_notes(const synth_chord_t *chord, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (chord == NULL) {
        return;
    }

    size_t used = 0;
    for (uint8_t i = 0; i < chord->note_count && used < out_size; ++i) {
        uint8_t midi_note = chord->notes[i];
        int written = snprintf(out + used,
                               out_size - used,
                               "%s%s%d(%u)",
                               i == 0 ? "" : " ",
                               synth_key_name((synth_key_t)(midi_note % 12)),
                               midi_note_octave(midi_note),
                               midi_note);
        if (written < 0) {
            out[used] = '\0';
            return;
        }
        if ((size_t)written >= out_size - used) {
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

void synth_app_make_root_note_chord(const synth_chord_t *source, synth_chord_t *note)
{
    if (source == NULL || note == NULL) {
        return;
    }

    *note = *source;
    note->notes[0] = source->root_midi;
    note->note_count = 1;
    note->bass_enabled = false;
    note->bass_mode = SYNTH_BASS_MODE_OFF;
    snprintf(note->name,
             sizeof(note->name),
             "%s",
             synth_key_name((synth_key_t)(source->root_midi % 12)));
}

bool synth_app_build_play_layer(const synth_chord_t held_chords[SYNTH_CHORD_BUTTON_COUNT],
                                const bool held_valid[SYNTH_CHORD_BUTTON_COUNT],
                                uint8_t held_mask,
                                synth_bass_mode_t bass_mode,
                                uint8_t slash_bass_slot,
                                synth_chord_t *layer)
{
    if (held_chords == NULL || held_valid == NULL || layer == NULL) {
        return false;
    }

    memset(layer, 0, sizeof(*layer));
    bool initialized = false;
    uint8_t chord_count = 0;
    uint8_t held_count = held_chord_count(held_valid, held_mask);

    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((held_mask & (1U << slot)) == 0 || !held_valid[slot]) {
            continue;
        }
        if (bass_mode == SYNTH_BASS_MODE_SLASH &&
            held_count > 1 &&
            slot == slash_bass_slot) {
            continue;
        }

        const synth_chord_t *chord = &held_chords[slot];
        if (!initialized) {
            *layer = *chord;
            layer->note_count = 0;
            initialized = true;
        }
        ++chord_count;
        for (uint8_t note_index = 0; note_index < chord->note_count; ++note_index) {
            append_unique_layer_note(layer, chord->notes[note_index]);
        }
    }

    if (!initialized || layer->note_count == 0) {
        return false;
    }
    if (chord_count > 1) {
        snprintf(layer->name, sizeof(layer->name), "%s", "LAYER");
    }
    return true;
}
