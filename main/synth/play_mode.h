#ifndef SYNTH_PLAY_MODE_H
#define SYNTH_PLAY_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_PLAY_MODE_PLAY = 0,
    SYNTH_PLAY_MODE_STRUM,
    SYNTH_PLAY_MODE_LEAD,
    SYNTH_PLAY_MODE_DRONE,
    SYNTH_PLAY_MODE_ARPEGGIO,
    SYNTH_PLAY_MODE_REPEAT,
    SYNTH_PLAY_MODE_MIC_SAMPLE,
    SYNTH_PLAY_MODE_DRUM_MODE,
    SYNTH_PLAY_MODE_DRUM_LOOPS,
    SYNTH_PLAY_MODE_AUTO_DRUM,
    SYNTH_PLAY_MODE_SEQUENCER,
    SYNTH_PLAY_MODE_CHORD_HIRO,
    SYNTH_PLAY_MODE_EAR_TRAINER,
    SYNTH_PLAY_MODE_TUNER,
    SYNTH_PLAY_MODE_MIXER,
    SYNTH_PLAY_MODE_COUNT,
} synth_play_mode_t;

typedef enum {
    SYNTH_STRUM_SPEED_SLOW = 0,
    SYNTH_STRUM_SPEED_MEDIUM,
    SYNTH_STRUM_SPEED_FAST,
    SYNTH_STRUM_SPEED_COUNT,
} synth_strum_speed_t;

typedef enum {
    SYNTH_ARP_PATTERN_UP = 0,
    SYNTH_ARP_PATTERN_DOWN,
    SYNTH_ARP_PATTERN_UP_DOWN,
    SYNTH_ARP_PATTERN_DOWN_UP,
    SYNTH_ARP_PATTERN_RANDOM,
    SYNTH_ARP_PATTERN_FINGERPICK,
    SYNTH_ARP_PATTERN_COUNT,
} synth_arp_pattern_t;

typedef enum {
    SYNTH_RHYTHM_RATE_WHOLE = 0,
    SYNTH_RHYTHM_RATE_HALF,
    SYNTH_RHYTHM_RATE_QUARTER,
    SYNTH_RHYTHM_RATE_EIGHTH,
    SYNTH_RHYTHM_RATE_SIXTEENTH,
    SYNTH_RHYTHM_RATE_SIXTEENTH_TRIPLET,
    SYNTH_RHYTHM_RATE_THIRTY_SECOND,
    SYNTH_RHYTHM_RATE_SWING_EIGHTH,
    SYNTH_RHYTHM_RATE_SWING_SIXTEENTH,
    SYNTH_RHYTHM_RATE_COUNT,
} synth_rhythm_rate_t;

typedef enum {
    SYNTH_ARP_CHORD_MODE_ARP_ONLY = 0,
    SYNTH_ARP_CHORD_MODE_CHORD_ARP,
    SYNTH_ARP_CHORD_MODE_RHYTHM_ARP,
    SYNTH_ARP_CHORD_MODE_COUNT,
} synth_arp_chord_mode_t;

const char *synth_play_mode_name(synth_play_mode_t mode);
const char *synth_play_mode_short_name(synth_play_mode_t mode);
bool synth_play_mode_is_implemented(synth_play_mode_t mode);
synth_play_mode_t synth_play_mode_step(synth_play_mode_t mode, int8_t steps);
synth_play_mode_t synth_play_mode_quick_select(uint8_t chord_button);

const char *synth_strum_speed_name(synth_strum_speed_t speed);
uint16_t synth_strum_speed_delay_ms(synth_strum_speed_t speed);
const char *synth_arp_pattern_name(synth_arp_pattern_t pattern);
const char *synth_rhythm_rate_name(synth_rhythm_rate_t rate);
const char *synth_arp_chord_mode_name(synth_arp_chord_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
