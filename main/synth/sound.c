#include "synth/sound.h"

#include <stddef.h>

typedef struct {
    const char *name;
    synth_engine_t engine;
} synth_sound_def_t;

static const synth_sound_def_t SOUNDS[SYNTH_SOUND_COUNT] = {
    [SYNTH_SOUND_SAW] = {.name = "SAW", .engine = SYNTH_ENGINE_ANALOG},
    [SYNTH_SOUND_SINE] = {.name = "SINE", .engine = SYNTH_ENGINE_ANALOG},
    [SYNTH_SOUND_TRIANGLE] = {.name = "TRIANGLE", .engine = SYNTH_ENGINE_ANALOG},
    [SYNTH_SOUND_SQUARE] = {.name = "SQUARE", .engine = SYNTH_ENGINE_ANALOG},
    [SYNTH_SOUND_FM_EPIANO] = {.name = "FM E.PIANO", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_FM_BELL] = {.name = "FM BELL", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_FM_HX7] = {.name = "FM HX7", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_STRINGS] = {.name = "STRINGS", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_FLUTE] = {.name = "FLUTE", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_BRASS] = {.name = "BRASS", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_CLARINET] = {.name = "CLARINET", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_WURLI] = {.name = "WURLI", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_VOX_AHH] = {.name = "VOX AHH", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_VIBRAPHONE] = {.name = "VIBRAPHONE", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_ACOUSTIC] = {.name = "ACOUSTIC", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_HARP] = {.name = "HARP", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_HUMMING] = {.name = "HUMMING", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_ROBBO] = {.name = "ROBBO", .engine = SYNTH_ENGINE_DX7},
    [SYNTH_SOUND_SHUTTER] = {.name = "SHUTTER", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_JUNO_POLY] = {.name = "JUNO POLY", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_OCEAN_PAD] = {.name = "OCEAN PAD", .engine = SYNTH_ENGINE_JUNO},
    [SYNTH_SOUND_SAWSQUARE] = {.name = "SAWSQUARE", .engine = SYNTH_ENGINE_ANALOG},
    [SYNTH_SOUND_MIC_SAMPLE] = {.name = "MIC SAMPLE", .engine = SYNTH_ENGINE_PCM},
    [SYNTH_SOUND_USER_SAMPLE] = {.name = "USER SAMPLE", .engine = SYNTH_ENGINE_PCM},
};

const char *synth_sound_name(synth_sound_t sound)
{
    return sound < SYNTH_SOUND_COUNT ? SOUNDS[sound].name : "?";
}

synth_engine_t synth_sound_engine(synth_sound_t sound)
{
    return sound < SYNTH_SOUND_COUNT ? SOUNDS[sound].engine : SYNTH_ENGINE_ANALOG;
}

const char *synth_sound_engine_name(synth_sound_t sound)
{
    switch (synth_sound_engine(sound)) {
    case SYNTH_ENGINE_ANALOG:
        return "ANALOG";
    case SYNTH_ENGINE_JUNO:
        return "JUNO";
    case SYNTH_ENGINE_DX7:
        return "DX7";
    case SYNTH_ENGINE_PCM:
        return "PCM";
    case SYNTH_ENGINE_SPECIAL:
        return "SPECIAL";
    default:
        return "?";
    }
}

synth_sound_t synth_sound_step(synth_sound_t sound, int8_t steps)
{
    int value = (int)sound + steps;
    while (value < 0) {
        value += SYNTH_SOUND_COUNT;
    }
    return (synth_sound_t)(value % SYNTH_SOUND_COUNT);
}

synth_sound_t synth_sound_quick_select(uint8_t chord_button)
{
    static const synth_sound_t QUICK_SELECT[] = {
        SYNTH_SOUND_SAW,
        SYNTH_SOUND_SINE,
        SYNTH_SOUND_FM_EPIANO,
        SYNTH_SOUND_FM_HX7,
        SYNTH_SOUND_STRINGS,
        SYNTH_SOUND_CLARINET,
        SYNTH_SOUND_FM_BELL,
    };

    if (chord_button == 0 || chord_button > sizeof(QUICK_SELECT) / sizeof(QUICK_SELECT[0])) {
        return SYNTH_SOUND_SAW;
    }
    return QUICK_SELECT[chord_button - 1];
}
