#ifndef SYNTH_APP_PLAY_STATE_H
#define SYNTH_APP_PLAY_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_APP_PLAY_NO_SLOT UINT8_MAX

#define SYNTH_APP_PLAY_STATE_INITIALIZER \
    { \
        .strum_speed = SYNTH_STRUM_SPEED_MEDIUM, \
        .arp_pattern = SYNTH_ARP_PATTERN_UP, \
        .arp_rate = SYNTH_RHYTHM_RATE_EIGHTH, \
        .arp_chord_mode = SYNTH_ARP_CHORD_MODE_ARP_ONLY, \
        .repeat_rate = SYNTH_RHYTHM_RATE_EIGHTH, \
        .strum_slot = SYNTH_APP_PLAY_NO_SLOT, \
        .arp_last_slot = SYNTH_APP_PLAY_NO_SLOT, \
        .repeat_slot = SYNTH_APP_PLAY_NO_SLOT, \
    }

typedef struct {
    synth_strum_speed_t strum_speed;
    synth_arp_pattern_t arp_pattern;
    synth_rhythm_rate_t arp_rate;
    synth_arp_chord_mode_t arp_chord_mode;
    synth_rhythm_rate_t repeat_rate;

    synth_chord_t strum_chord;
    bool strum_active;
    uint8_t strum_slot;
    uint8_t strum_voice_index;
    TickType_t strum_next_tick;

    synth_chord_t arp_chords[SYNTH_CHORD_BUTTON_COUNT];
    bool arp_chord_valid[SYNTH_CHORD_BUTTON_COUNT];
    uint32_t arp_step_index;
    uint8_t arp_last_slot;
    TickType_t arp_next_tick;

    bool repeat_active;
    bool repeat_gate_open;
    uint8_t repeat_slot;
    TickType_t repeat_next_tick;
} synth_app_play_state_t;

void synth_app_play_stop_strum(synth_app_play_state_t *play);
void synth_app_play_start_strum(synth_app_play_state_t *play,
                                uint8_t slot,
                                const synth_chord_t *chord,
                                TickType_t now);
bool synth_app_play_arp_clock_active(const synth_app_play_state_t *play);
void synth_app_play_queue_arp_source(synth_app_play_state_t *play,
                                     uint8_t slot,
                                     const synth_chord_t *chord,
                                     TickType_t now);
void synth_app_play_clear_arp_source(synth_app_play_state_t *play, uint8_t slot);
void synth_app_play_stop_arp(synth_app_play_state_t *play);
void synth_app_play_clear_arp_sources(synth_app_play_state_t *play, TickType_t now);
bool synth_app_play_repeat_clock_active(const synth_app_play_state_t *play);
void synth_app_play_queue_repeat_source(synth_app_play_state_t *play,
                                        uint8_t slot,
                                        TickType_t now);
void synth_app_play_stop_repeat(synth_app_play_state_t *play);

#ifdef __cplusplus
}
#endif

#endif
