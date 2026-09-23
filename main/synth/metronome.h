#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t beat_frames;
    uint64_t anchor_frame;
    bool enabled;
} synth_metronome_t;

uint32_t synth_metronome_beat_frames(uint32_t sample_rate_hz, uint16_t bpm);

void synth_metronome_init(synth_metronome_t *metronome, uint32_t sample_rate_hz);
void synth_metronome_set_enabled(synth_metronome_t *metronome,
                                 bool enabled,
                                 uint16_t bpm,
                                 uint64_t anchor_frame);
bool synth_metronome_toggle(synth_metronome_t *metronome,
                            uint16_t bpm,
                            uint64_t anchor_frame);
void synth_metronome_set_timing(synth_metronome_t *metronome,
                                uint16_t bpm,
                                uint64_t anchor_frame);
void synth_metronome_set_bpm_preserve_phase(synth_metronome_t *metronome,
                                            uint16_t bpm,
                                            uint64_t current_frame);
bool synth_metronome_is_enabled(const synth_metronome_t *metronome);
int32_t synth_metronome_sample(const synth_metronome_t *metronome, uint64_t global_frame);
