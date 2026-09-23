#include "synth/metronome.h"

#include <stddef.h>

static const uint16_t CLICK_FRAMES = 220;
static const int32_t CLICK_AMP = 4200;
static const int32_t CLICK_ACCENT_AMP = 6200;

uint32_t synth_metronome_beat_frames(uint32_t sample_rate_hz, uint16_t bpm)
{
    if (sample_rate_hz == 0) {
        return 0;
    }
    if (bpm == 0) {
        bpm = 120;
    }
    return (uint32_t)((60ULL * (uint64_t)sample_rate_hz) / (uint64_t)bpm);
}

void synth_metronome_init(synth_metronome_t *metronome, uint32_t sample_rate_hz)
{
    if (metronome == NULL) {
        return;
    }

    metronome->sample_rate_hz = sample_rate_hz;
    metronome->beat_frames = synth_metronome_beat_frames(sample_rate_hz, 120);
    metronome->anchor_frame = 0;
    metronome->enabled = false;
}

void synth_metronome_set_timing(synth_metronome_t *metronome,
                                uint16_t bpm,
                                uint64_t anchor_frame)
{
    if (metronome == NULL) {
        return;
    }

    metronome->beat_frames = synth_metronome_beat_frames(metronome->sample_rate_hz, bpm);
    metronome->anchor_frame = anchor_frame;
}

void synth_metronome_set_bpm_preserve_phase(synth_metronome_t *metronome,
                                            uint16_t bpm,
                                            uint64_t current_frame)
{
    if (metronome == NULL) {
        return;
    }

    uint32_t old_beat_frames = metronome->beat_frames;
    uint32_t new_beat_frames = synth_metronome_beat_frames(metronome->sample_rate_hz, bpm);
    if (old_beat_frames == 0 || new_beat_frames == 0 || current_frame < metronome->anchor_frame) {
        synth_metronome_set_timing(metronome, bpm, current_frame);
        return;
    }

    uint64_t old_bar_frames = (uint64_t)old_beat_frames * 4ULL;
    uint64_t old_bar_phase = (current_frame - metronome->anchor_frame) % old_bar_frames;
    uint64_t new_bar_phase = (old_bar_phase * (uint64_t)new_beat_frames) /
                             (uint64_t)old_beat_frames;
    uint64_t new_bar_frames = (uint64_t)new_beat_frames * 4ULL;
    if (new_bar_phase >= new_bar_frames) {
        new_bar_phase %= new_bar_frames;
    }

    metronome->beat_frames = new_beat_frames;
    metronome->anchor_frame = current_frame >= new_bar_phase ? current_frame - new_bar_phase : 0;
}

void synth_metronome_set_enabled(synth_metronome_t *metronome,
                                 bool enabled,
                                 uint16_t bpm,
                                 uint64_t anchor_frame)
{
    if (metronome == NULL) {
        return;
    }

    metronome->enabled = enabled;
    if (enabled) {
        synth_metronome_set_timing(metronome, bpm, anchor_frame);
    }
}

bool synth_metronome_toggle(synth_metronome_t *metronome,
                            uint16_t bpm,
                            uint64_t anchor_frame)
{
    if (metronome == NULL) {
        return false;
    }

    bool enabled = !metronome->enabled;
    synth_metronome_set_enabled(metronome, enabled, bpm, anchor_frame);
    return enabled;
}

bool synth_metronome_is_enabled(const synth_metronome_t *metronome)
{
    return metronome != NULL && metronome->enabled;
}

int32_t synth_metronome_sample(const synth_metronome_t *metronome, uint64_t global_frame)
{
    if (metronome == NULL ||
        !metronome->enabled ||
        metronome->beat_frames == 0 ||
        global_frame < metronome->anchor_frame) {
        return 0;
    }

    uint64_t metronome_offset = global_frame - metronome->anchor_frame;
    uint32_t beat_offset = (uint32_t)(metronome_offset % metronome->beat_frames);
    if (beat_offset >= CLICK_FRAMES) {
        return 0;
    }

    uint32_t beat_index = (uint32_t)((metronome_offset / metronome->beat_frames) % 4ULL);
    int32_t amplitude = beat_index == 0 ? CLICK_ACCENT_AMP : CLICK_AMP;
    int32_t envelope = (amplitude * (int32_t)(CLICK_FRAMES - beat_offset)) /
                       (int32_t)CLICK_FRAMES;
    return ((beat_offset / 8U) & 1U) != 0 ? -envelope : envelope;
}
