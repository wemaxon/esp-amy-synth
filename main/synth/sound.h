#ifndef SYNTH_SOUND_H
#define SYNTH_SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_SOUND_SAW = 0,
    SYNTH_SOUND_SINE,
    SYNTH_SOUND_FM_EPIANO,
    SYNTH_SOUND_FM_HX7,
    SYNTH_SOUND_STRINGS,
    SYNTH_SOUND_CLARINET,
    SYNTH_SOUND_FM_BELL,
    SYNTH_SOUND_TRIANGLE,
    SYNTH_SOUND_SQUARE,
    SYNTH_SOUND_PIANO,
    SYNTH_SOUND_JUNO_POLY,
    SYNTH_SOUND_COUNT,
} synth_sound_t;

typedef enum {
    SYNTH_ENGINE_ANALOG = 0,
    SYNTH_ENGINE_FM,
    SYNTH_ENGINE_SAMPLE,
    SYNTH_ENGINE_HYBRID,
} synth_engine_t;

const char *synth_sound_name(synth_sound_t sound);
const char *synth_sound_engine_name(synth_sound_t sound);
synth_engine_t synth_sound_engine(synth_sound_t sound);
synth_sound_t synth_sound_step(synth_sound_t sound, int8_t steps);
synth_sound_t synth_sound_quick_select(uint8_t chord_button);

#ifdef __cplusplus
}
#endif

#endif
