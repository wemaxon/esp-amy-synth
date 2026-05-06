#ifndef SYNTH_PLAY_MODE_H
#define SYNTH_PLAY_MODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_PLAY_MODE_PLAY = 0,
    SYNTH_PLAY_MODE_STRUM,
    SYNTH_PLAY_MODE_LEAD,
    SYNTH_PLAY_MODE_DRONE,
    SYNTH_PLAY_MODE_ARP,
    SYNTH_PLAY_MODE_REPEAT,
    SYNTH_PLAY_MODE_DRUMS,
    SYNTH_PLAY_MODE_DRUM_LOOPS,
    SYNTH_PLAY_MODE_SEQUENCER,
    SYNTH_PLAY_MODE_LOOPER,
    SYNTH_PLAY_MODE_MIXER,
    SYNTH_PLAY_MODE_COUNT,
} synth_play_mode_t;

const char *synth_play_mode_name(synth_play_mode_t mode);
synth_play_mode_t synth_play_mode_step(synth_play_mode_t mode, int8_t steps);

#ifdef __cplusplus
}
#endif

#endif
