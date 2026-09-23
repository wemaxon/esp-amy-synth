#include "synth/level_calibration.h"

#include "synth/level_calibration.generated.h"

_Static_assert(sizeof(SYNTH_LEVEL_SOUND_GAIN) / sizeof(SYNTH_LEVEL_SOUND_GAIN[0]) ==
                   SYNTH_SOUND_COUNT,
               "sound calibration table must cover the catalog");
_Static_assert(sizeof(SYNTH_LEVEL_DRUM_GAIN) / sizeof(SYNTH_LEVEL_DRUM_GAIN[0]) ==
                   SYNTH_DRUM_KIT_COUNT,
               "drum calibration table must cover every kit");
_Static_assert(sizeof(SYNTH_LEVEL_DRUM_GAIN[0]) / sizeof(SYNTH_LEVEL_DRUM_GAIN[0][0]) ==
                   SYNTH_DRUM_VOICE_COUNT,
               "drum calibration table must cover every role");

float synth_level_calibration_sound_gain(synth_sound_t sound)
{
    return sound < SYNTH_SOUND_COUNT ? SYNTH_LEVEL_SOUND_GAIN[sound] : 1.0f;
}

float synth_level_calibration_drum_gain(synth_drum_kit_t kit, synth_drum_voice_t voice)
{
    if (kit >= SYNTH_DRUM_KIT_COUNT || voice >= SYNTH_DRUM_VOICE_COUNT) {
        return 1.0f;
    }
    return SYNTH_LEVEL_DRUM_GAIN[kit][voice];
}

const char *synth_level_calibration_revision(void)
{
    return SYNTH_LEVEL_CALIBRATION_REVISION;
}
