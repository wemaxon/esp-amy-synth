#ifndef SYNTH_LEVEL_CALIBRATION_H
#define SYNTH_LEVEL_CALIBRATION_H

#include "synth/drums.h"
#include "synth/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Static, reviewed gains generated from an on-device calibration report. */
float synth_level_calibration_sound_gain(synth_sound_t sound);
float synth_level_calibration_drum_gain(synth_drum_kit_t kit, synth_drum_voice_t voice);
const char *synth_level_calibration_revision(void);

#ifdef __cplusplus
}
#endif

#endif
