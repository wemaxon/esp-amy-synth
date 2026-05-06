#include "synth/play_mode.h"

static const char *PLAY_MODE_NAMES[SYNTH_PLAY_MODE_COUNT] = {
    [SYNTH_PLAY_MODE_PLAY] = "PLAY",
    [SYNTH_PLAY_MODE_STRUM] = "STRUM",
    [SYNTH_PLAY_MODE_LEAD] = "LEAD",
    [SYNTH_PLAY_MODE_DRONE] = "DRONE",
    [SYNTH_PLAY_MODE_ARP] = "ARP",
    [SYNTH_PLAY_MODE_REPEAT] = "REPEAT",
    [SYNTH_PLAY_MODE_DRUMS] = "DRUMS",
    [SYNTH_PLAY_MODE_DRUM_LOOPS] = "DRUM LOOP",
    [SYNTH_PLAY_MODE_SEQUENCER] = "SEQ",
    [SYNTH_PLAY_MODE_LOOPER] = "LOOPER",
    [SYNTH_PLAY_MODE_MIXER] = "MIXER",
};

const char *synth_play_mode_name(synth_play_mode_t mode)
{
    return mode < SYNTH_PLAY_MODE_COUNT ? PLAY_MODE_NAMES[mode] : "?";
}

synth_play_mode_t synth_play_mode_step(synth_play_mode_t mode, int8_t steps)
{
    int value = (int)mode + steps;
    while (value < 0) {
        value += SYNTH_PLAY_MODE_COUNT;
    }
    return (synth_play_mode_t)(value % SYNTH_PLAY_MODE_COUNT);
}
