#include "synth/play_mode.h"

static const char *PLAY_MODE_NAMES[SYNTH_PLAY_MODE_COUNT] = {
    [SYNTH_PLAY_MODE_PLAY] = "PLAY ONESHOT",
    [SYNTH_PLAY_MODE_STRUM] = "STRUM",
    [SYNTH_PLAY_MODE_LEAD] = "LEAD",
    [SYNTH_PLAY_MODE_DRONE] = "DRONE",
    [SYNTH_PLAY_MODE_ARPEGGIO] = "ARPEGGIO",
    [SYNTH_PLAY_MODE_REPEAT] = "REPEAT",
    [SYNTH_PLAY_MODE_MIC_SAMPLE] = "MIC SAMPLE",
    [SYNTH_PLAY_MODE_DRUM_MODE] = "DRUM MODE",
    [SYNTH_PLAY_MODE_DRUM_LOOPS] = "DRUM LOOP",
    [SYNTH_PLAY_MODE_AUTO_DRUM] = "AUTO DRUM",
    [SYNTH_PLAY_MODE_SEQUENCER] = "SEQ",
    [SYNTH_PLAY_MODE_CHORD_HIRO] = "CHORD HIRO",
    [SYNTH_PLAY_MODE_EAR_TRAINER] = "EAR TRAIN",
    [SYNTH_PLAY_MODE_TUNER] = "TUNER",
    [SYNTH_PLAY_MODE_MIXER] = "MIXER",
};

static const char *PLAY_MODE_SHORT_NAMES[SYNTH_PLAY_MODE_COUNT] = {
    [SYNTH_PLAY_MODE_PLAY] = "PLAY",
    [SYNTH_PLAY_MODE_STRUM] = "STRUM",
    [SYNTH_PLAY_MODE_LEAD] = "LEAD",
    [SYNTH_PLAY_MODE_DRONE] = "DRONE",
    [SYNTH_PLAY_MODE_ARPEGGIO] = "ARP",
    [SYNTH_PLAY_MODE_REPEAT] = "REPEAT",
    [SYNTH_PLAY_MODE_MIC_SAMPLE] = "MIC",
    [SYNTH_PLAY_MODE_DRUM_MODE] = "DRUM",
    [SYNTH_PLAY_MODE_DRUM_LOOPS] = "BEAT",
    [SYNTH_PLAY_MODE_AUTO_DRUM] = "AUTO",
    [SYNTH_PLAY_MODE_SEQUENCER] = "SEQ",
    [SYNTH_PLAY_MODE_CHORD_HIRO] = "HIRO",
    [SYNTH_PLAY_MODE_EAR_TRAINER] = "EAR",
    [SYNTH_PLAY_MODE_TUNER] = "TUNER",
    [SYNTH_PLAY_MODE_MIXER] = "MIX",
};

static const char *STRUM_SPEED_NAMES[SYNTH_STRUM_SPEED_COUNT] = {
    [SYNTH_STRUM_SPEED_SLOW] = "SLOW",
    [SYNTH_STRUM_SPEED_MEDIUM] = "MED",
    [SYNTH_STRUM_SPEED_FAST] = "FAST",
};

static const uint16_t STRUM_SPEED_DELAYS_MS[SYNTH_STRUM_SPEED_COUNT] = {
    [SYNTH_STRUM_SPEED_SLOW] = 120,
    [SYNTH_STRUM_SPEED_MEDIUM] = 80,
    [SYNTH_STRUM_SPEED_FAST] = 40,
};

static const char *ARP_PATTERN_NAMES[SYNTH_ARP_PATTERN_COUNT] = {
    [SYNTH_ARP_PATTERN_UP] = "UP",
    [SYNTH_ARP_PATTERN_DOWN] = "DOWN",
    [SYNTH_ARP_PATTERN_UP_DOWN] = "UP/DN",
    [SYNTH_ARP_PATTERN_DOWN_UP] = "DN/UP",
    [SYNTH_ARP_PATTERN_RANDOM] = "RANDOM",
    [SYNTH_ARP_PATTERN_FINGERPICK] = "FINGER",
};

static const char *RHYTHM_RATE_NAMES[SYNTH_RHYTHM_RATE_COUNT] = {
    [SYNTH_RHYTHM_RATE_WHOLE] = "1/1",
    [SYNTH_RHYTHM_RATE_HALF] = "1/2",
    [SYNTH_RHYTHM_RATE_QUARTER] = "1/4",
    [SYNTH_RHYTHM_RATE_EIGHTH] = "1/8",
    [SYNTH_RHYTHM_RATE_SIXTEENTH] = "1/16",
    [SYNTH_RHYTHM_RATE_SIXTEENTH_TRIPLET] = "1/16T",
    [SYNTH_RHYTHM_RATE_THIRTY_SECOND] = "1/32",
    [SYNTH_RHYTHM_RATE_SWING_EIGHTH] = "SW8",
    [SYNTH_RHYTHM_RATE_SWING_SIXTEENTH] = "SW16",
};

static const char *ARP_CHORD_MODE_NAMES[SYNTH_ARP_CHORD_MODE_COUNT] = {
    [SYNTH_ARP_CHORD_MODE_ARP_ONLY] = "ARP ONLY",
    [SYNTH_ARP_CHORD_MODE_CHORD_ARP] = "CHORD+ARP",
    [SYNTH_ARP_CHORD_MODE_RHYTHM_ARP] = "RHYTHM+ARP",
};

const char *synth_play_mode_name(synth_play_mode_t mode)
{
    return mode < SYNTH_PLAY_MODE_COUNT ? PLAY_MODE_NAMES[mode] : "?";
}

const char *synth_play_mode_short_name(synth_play_mode_t mode)
{
    return mode < SYNTH_PLAY_MODE_COUNT ? PLAY_MODE_SHORT_NAMES[mode] : "?";
}

bool synth_play_mode_is_implemented(synth_play_mode_t mode)
{
    switch (mode) {
    case SYNTH_PLAY_MODE_PLAY:
    case SYNTH_PLAY_MODE_STRUM:
    case SYNTH_PLAY_MODE_LEAD:
    case SYNTH_PLAY_MODE_DRONE:
    case SYNTH_PLAY_MODE_ARPEGGIO:
    case SYNTH_PLAY_MODE_REPEAT:
    case SYNTH_PLAY_MODE_MIXER:
        return true;
    default:
        return false;
    }
}

synth_play_mode_t synth_play_mode_step(synth_play_mode_t mode, int8_t steps)
{
    int value = (int)mode + steps;
    while (value < 0) {
        value += SYNTH_PLAY_MODE_COUNT;
    }
    return (synth_play_mode_t)(value % SYNTH_PLAY_MODE_COUNT);
}

synth_play_mode_t synth_play_mode_quick_select(uint8_t chord_button)
{
    static const synth_play_mode_t QUICK_SELECT[] = {
        SYNTH_PLAY_MODE_PLAY,
        SYNTH_PLAY_MODE_STRUM,
        SYNTH_PLAY_MODE_LEAD,
        SYNTH_PLAY_MODE_DRONE,
        SYNTH_PLAY_MODE_ARPEGGIO,
        SYNTH_PLAY_MODE_REPEAT,
        SYNTH_PLAY_MODE_MIC_SAMPLE,
    };

    if (chord_button == 0 || chord_button > sizeof(QUICK_SELECT) / sizeof(QUICK_SELECT[0])) {
        return SYNTH_PLAY_MODE_PLAY;
    }
    return QUICK_SELECT[chord_button - 1];
}

const char *synth_strum_speed_name(synth_strum_speed_t speed)
{
    return speed < SYNTH_STRUM_SPEED_COUNT ? STRUM_SPEED_NAMES[speed] : "?";
}

uint16_t synth_strum_speed_delay_ms(synth_strum_speed_t speed)
{
    return speed < SYNTH_STRUM_SPEED_COUNT ? STRUM_SPEED_DELAYS_MS[speed] : STRUM_SPEED_DELAYS_MS[SYNTH_STRUM_SPEED_MEDIUM];
}

const char *synth_arp_pattern_name(synth_arp_pattern_t pattern)
{
    return pattern < SYNTH_ARP_PATTERN_COUNT ? ARP_PATTERN_NAMES[pattern] : "?";
}

const char *synth_rhythm_rate_name(synth_rhythm_rate_t rate)
{
    return rate < SYNTH_RHYTHM_RATE_COUNT ? RHYTHM_RATE_NAMES[rate] : "?";
}

const char *synth_arp_chord_mode_name(synth_arp_chord_mode_t mode)
{
    return mode < SYNTH_ARP_CHORD_MODE_COUNT ? ARP_CHORD_MODE_NAMES[mode] : "?";
}
