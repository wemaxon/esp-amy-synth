#include "synth/drums.h"

#include <stddef.h>

#include "synth/amy_engine.h"
#include "synth/beat_scheduler.h"
#include "synth/level_calibration.h"

#define DRUM_BIT(voice) ((uint8_t)(1U << (uint8_t)(voice)))

typedef struct {
    uint8_t preset;
    uint8_t midi_note;
} synth_drum_sample_t;

typedef struct {
    const char *name;
    synth_drum_sample_t sample[SYNTH_DRUM_VOICE_COUNT];
    uint8_t velocity_percent;
} synth_drum_kit_def_t;

enum {
    DRUM_PRESET_KICK = 1,
    DRUM_PRESET_SNARE = 2,
    DRUM_PRESET_SNARE_ALT = 5,
    DRUM_PRESET_CLOSED_HAT = 6,
    DRUM_PRESET_OPEN_HAT = 7,
    DRUM_PRESET_TOM = 8,
    DRUM_PRESET_CLAP = 9,
    DRUM_PRESET_COWBELL = 10,
};

enum {
    DRUM_NOTE_KICK = 39,
    DRUM_NOTE_KICK_ALT = 43,
    DRUM_NOTE_SNARE = 45,
    DRUM_NOTE_SNARE_ALT = 41,
    DRUM_NOTE_CLOSED_HAT = 53,
    DRUM_NOTE_OPEN_HAT = 56,
    DRUM_NOTE_TOM_LOW = 61,
    DRUM_NOTE_TOM_HIGH = 68,
    DRUM_NOTE_CLAP = 94,
    DRUM_NOTE_COWBELL = 69,
};

#define DRUM_SAMPLE(_preset, _note) \
    { .preset = (_preset), .midi_note = (_note) }

static const synth_drum_kit_def_t DRUM_KITS[SYNTH_DRUM_KIT_COUNT] = {
    [SYNTH_DRUM_KIT_TIGHT] = {
        .name = "TIGHT",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_SNARE, DRUM_NOTE_SNARE),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_LOW),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 96,
    },
    [SYNTH_DRUM_KIT_X0X] = {
        .name = "X0X",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_SNARE_ALT, DRUM_NOTE_SNARE_ALT),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_HIGH),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 100,
    },
    [SYNTH_DRUM_KIT_X9X] = {
        .name = "X9X",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_CLAP, DRUM_NOTE_CLAP),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_LOW),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 98,
    },
    [SYNTH_DRUM_KIT_LYNN] = {
        .name = "LYNN",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_SNARE_ALT, DRUM_NOTE_SNARE_ALT),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_HIGH),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 92,
    },
    [SYNTH_DRUM_KIT_KR78] = {
        .name = "KR-78",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_SNARE, DRUM_NOTE_SNARE),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_LOW),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 88,
    },
    [SYNTH_DRUM_KIT_TRAP] = {
        .name = "TRAP",
        .sample = {
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK),
            DRUM_SAMPLE(DRUM_PRESET_KICK, DRUM_NOTE_KICK_ALT),
            DRUM_SAMPLE(DRUM_PRESET_CLAP, DRUM_NOTE_CLAP),
            DRUM_SAMPLE(DRUM_PRESET_CLOSED_HAT, DRUM_NOTE_CLOSED_HAT),
            DRUM_SAMPLE(DRUM_PRESET_TOM, DRUM_NOTE_TOM_HIGH),
            DRUM_SAMPLE(DRUM_PRESET_COWBELL, DRUM_NOTE_COWBELL),
            DRUM_SAMPLE(DRUM_PRESET_OPEN_HAT, DRUM_NOTE_OPEN_HAT),
        },
        .velocity_percent = 104,
    },
};

#undef DRUM_SAMPLE

static const char *const DRUM_VOICE_NAMES[SYNTH_DRUM_VOICE_COUNT] = {
    [SYNTH_DRUM_VOICE_KICK] = "KICK",
    [SYNTH_DRUM_VOICE_KICK_ALT] = "KICK ALT",
    [SYNTH_DRUM_VOICE_SNARE] = "SNARE",
    [SYNTH_DRUM_VOICE_CLOSED_HAT] = "CLOSED HAT",
    [SYNTH_DRUM_VOICE_TOM] = "TOM",
    [SYNTH_DRUM_VOICE_BELL] = "BELL",
    [SYNTH_DRUM_VOICE_OPEN_HAT] = "OPEN HAT",
};

static const char *const LOOP_STYLE_NAMES[SYNTH_DRUM_LOOP_STYLE_COUNT] = {
    [SYNTH_DRUM_LOOP_ROCK] = "ROCK",
    [SYNTH_DRUM_LOOP_DISCO] = "DISCO",
    [SYNTH_DRUM_LOOP_REGGAE] = "REGGAE",
    [SYNTH_DRUM_LOOP_FUNK] = "FUNK",
    [SYNTH_DRUM_LOOP_HIP_HOP] = "HIP-HOP",
    [SYNTH_DRUM_LOOP_ELECTRO] = "ELECTRO",
    [SYNTH_DRUM_LOOP_JAZZ] = "JAZZ",
};

static const char *const LOOP_VARIATION_NAMES[SYNTH_DRUM_LOOP_VARIATION_COUNT] = {
    [SYNTH_DRUM_LOOP_VARIATION_ORIGINAL] = "ORIGINAL",
    [SYNTH_DRUM_LOOP_VARIATION_GHOSTS] = "GHOSTS",
    [SYNTH_DRUM_LOOP_VARIATION_BUSY_HATS] = "BUSY HATS",
    [SYNTH_DRUM_LOOP_VARIATION_SYNC_KICKS] = "SYNC KICKS",
    [SYNTH_DRUM_LOOP_VARIATION_FILLS] = "FILLS",
    [SYNTH_DRUM_LOOP_VARIATION_HALF_TIME] = "HALF-TIME",
    [SYNTH_DRUM_LOOP_VARIATION_DOUBLE_TIME] = "DOUBLE-TIME",
    [SYNTH_DRUM_LOOP_VARIATION_JAZZ] = "JAZZ",
};

static const char *const LOOP_VARIATION_SHORT_NAMES[SYNTH_DRUM_LOOP_VARIATION_COUNT] = {
    [SYNTH_DRUM_LOOP_VARIATION_ORIGINAL] = "ORIG",
    [SYNTH_DRUM_LOOP_VARIATION_GHOSTS] = "GHOST",
    [SYNTH_DRUM_LOOP_VARIATION_BUSY_HATS] = "BUSY",
    [SYNTH_DRUM_LOOP_VARIATION_SYNC_KICKS] = "SYNC",
    [SYNTH_DRUM_LOOP_VARIATION_FILLS] = "FILL",
    [SYNTH_DRUM_LOOP_VARIATION_HALF_TIME] = "HALF",
    [SYNTH_DRUM_LOOP_VARIATION_DOUBLE_TIME] = "DBL",
    [SYNTH_DRUM_LOOP_VARIATION_JAZZ] = "JAZZ",
};

static bool on_step(uint8_t step, uint16_t mask)
{
    return (mask & (uint16_t)(1U << (step % SYNTH_DRUM_PATTERN_STEPS))) != 0;
}

static synth_drum_pattern_step_t make_step(uint8_t mask, uint8_t accent_mask)
{
    return (synth_drum_pattern_step_t){
        .mask = mask,
        .accent_mask = (uint8_t)(accent_mask & mask),
    };
}

static synth_drum_pattern_step_t base_pattern(synth_drum_loop_style_t style, uint8_t step)
{
    uint8_t mask = 0;
    uint8_t accent = 0;

    switch (style) {
    case SYNTH_DRUM_LOOP_DISCO:
        if ((step % 4U) == 0) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if ((step % 4U) == 2) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_OPEN_HAT);
        } else if ((step % 2U) == 0) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_REGGAE:
        if (step == 8) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if (step == 2 || step == 6 || step == 10 || step == 14) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_FUNK:
        if (step == 0 || step == 6 || step == 10) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if ((step % 2U) == 0) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_HIP_HOP:
        if (step == 0 || step == 7 || step == 10) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if (step == 0 || step == 2 || step == 6 || step == 8 || step == 10 || step == 14) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_ELECTRO:
        if ((step % 4U) == 0) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        if (step == 3 || step == 11) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_BELL);
        }
        break;
    case SYNTH_DRUM_LOOP_JAZZ:
        if (on_step(step, 0x4949U)) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_BELL);
        }
        if (step == 0 || step == 8) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        break;
    case SYNTH_DRUM_LOOP_ROCK:
    default:
        if (step == 0 || step == 8) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 4 || step == 12) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            accent |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if ((step % 2U) == 0) {
            mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    }

    return make_step(mask, accent);
}

const char *synth_drum_voice_name(synth_drum_voice_t voice)
{
    return voice < SYNTH_DRUM_VOICE_COUNT ? DRUM_VOICE_NAMES[voice] : "?";
}

const char *synth_drum_kit_name(synth_drum_kit_t kit)
{
    return kit < SYNTH_DRUM_KIT_COUNT ? DRUM_KITS[kit].name : "?";
}

const char *synth_drum_loop_style_name(synth_drum_loop_style_t style)
{
    return style < SYNTH_DRUM_LOOP_STYLE_COUNT ? LOOP_STYLE_NAMES[style] : "?";
}

const char *synth_drum_loop_variation_name(synth_drum_loop_variation_t variation)
{
    return variation < SYNTH_DRUM_LOOP_VARIATION_COUNT ? LOOP_VARIATION_NAMES[variation] : "?";
}

const char *synth_drum_loop_variation_short_name(synth_drum_loop_variation_t variation)
{
    return variation < SYNTH_DRUM_LOOP_VARIATION_COUNT ? LOOP_VARIATION_SHORT_NAMES[variation] : "?";
}

synth_drum_kit_t synth_drum_kit_for_button(uint8_t button)
{
    if (button == 0 || button > SYNTH_DRUM_BUTTON_COUNT) {
        return SYNTH_DRUM_KIT_TIGHT;
    }
    if (button > SYNTH_DRUM_KIT_COUNT) {
        return SYNTH_DRUM_KIT_TIGHT;
    }
    return (synth_drum_kit_t)(button - 1U);
}

synth_drum_kit_t synth_drum_kit_step(synth_drum_kit_t kit, int8_t steps)
{
    int value = (int)kit + steps;
    while (value < 0) {
        value += SYNTH_DRUM_KIT_COUNT;
    }
    return (synth_drum_kit_t)(value % SYNTH_DRUM_KIT_COUNT);
}

synth_drum_voice_t synth_drum_voice_for_button(uint8_t button)
{
    if (button == 0 || button > SYNTH_DRUM_BUTTON_COUNT) {
        return SYNTH_DRUM_VOICE_KICK;
    }
    return (synth_drum_voice_t)(button - 1U);
}

synth_drum_loop_style_t synth_drum_loop_style_for_button(uint8_t button)
{
    if (button == 0 || button > SYNTH_DRUM_BUTTON_COUNT) {
        return SYNTH_DRUM_LOOP_ROCK;
    }
    return (synth_drum_loop_style_t)(button - 1U);
}

synth_drum_loop_variation_t synth_drum_loop_variation_step(synth_drum_loop_variation_t variation,
                                                           int8_t steps)
{
    int value = (int)variation + steps;
    while (value < 0) {
        value += SYNTH_DRUM_LOOP_VARIATION_COUNT;
    }
    return (synth_drum_loop_variation_t)(value % SYNTH_DRUM_LOOP_VARIATION_COUNT);
}

synth_drum_pattern_step_t synth_drum_pattern_step(synth_drum_loop_style_t style,
                                                  synth_drum_loop_variation_t variation,
                                                  uint8_t step)
{
    step %= SYNTH_DRUM_PATTERN_STEPS;
    synth_drum_pattern_step_t result = base_pattern(style, step);

    switch (variation) {
    case SYNTH_DRUM_LOOP_VARIATION_GHOSTS:
        if (step == 3 || step == 11) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        break;
    case SYNTH_DRUM_LOOP_VARIATION_BUSY_HATS:
        result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        break;
    case SYNTH_DRUM_LOOP_VARIATION_SYNC_KICKS:
        if (step == 5 || step == 11 || step == 14) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK_ALT);
        }
        break;
    case SYNTH_DRUM_LOOP_VARIATION_FILLS:
        if (step >= 12) {
            result.mask |= (step & 1U) != 0 ?
                DRUM_BIT(SYNTH_DRUM_VOICE_TOM) :
                DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            result.accent_mask |= result.mask;
        }
        break;
    case SYNTH_DRUM_LOOP_VARIATION_HALF_TIME:
        result = make_step(0, 0);
        if (step == 0 || step == 10) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
            result.accent_mask |= DRUM_BIT(SYNTH_DRUM_VOICE_KICK);
        }
        if (step == 8) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
            result.accent_mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        if ((step % 2U) == 0) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_VARIATION_DOUBLE_TIME:
        if (step == 2 || step == 6 || step == 10 || step == 14) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_SNARE);
        }
        result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        break;
    case SYNTH_DRUM_LOOP_VARIATION_JAZZ:
        result.mask |= on_step(step, 0x4949U) ?
            DRUM_BIT(SYNTH_DRUM_VOICE_BELL) :
            0;
        if (step == 6 || step == 14) {
            result.mask |= DRUM_BIT(SYNTH_DRUM_VOICE_CLOSED_HAT);
        }
        break;
    case SYNTH_DRUM_LOOP_VARIATION_ORIGINAL:
    default:
        break;
    }

    result.accent_mask &= result.mask;
    return result;
}

uint8_t synth_drums_default_velocity(synth_drum_voice_t voice, bool accented)
{
    static const uint8_t NORMAL[SYNTH_DRUM_VOICE_COUNT] = {104, 96, 94, 54, 80, 64, 70};
    static const uint8_t ACCENT[SYNTH_DRUM_VOICE_COUNT] = {120, 112, 116, 70, 98, 82, 86};

    if (voice >= SYNTH_DRUM_VOICE_COUNT) {
        return accented ? 112 : 80;
    }
    return accented ? ACCENT[voice] : NORMAL[voice];
}

esp_err_t synth_drums_hit(synth_drum_kit_t kit, synth_drum_voice_t voice, uint8_t velocity)
{
    return synth_drums_hit_at(kit, voice, velocity, 0);
}

esp_err_t synth_drums_hit_at(synth_drum_kit_t kit,
                             synth_drum_voice_t voice,
                             uint8_t velocity,
                             uint16_t sample_offset)
{
    if (kit >= SYNTH_DRUM_KIT_COUNT || voice >= SYNTH_DRUM_VOICE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t scaled = (uint16_t)velocity * (uint16_t)DRUM_KITS[kit].velocity_percent;
    scaled = (uint16_t)((scaled + 50U) / 100U);
    if (scaled > 127U) {
        scaled = 127U;
    }
    const synth_drum_sample_t *sample = &DRUM_KITS[kit].sample[voice];
    return synth_amy_drum_hit_at(
        sample->preset,
        sample->midi_note,
        (uint8_t)scaled,
        synth_level_calibration_drum_gain(kit, voice),
        sample_offset
    );
}
