#ifndef SYNTH_AMY_VOICE_MAP_H
#define SYNTH_AMY_VOICE_MAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "synth/amy_runtime.h"
#include "synth/chord_engine.h"

typedef struct {
    bool enabled;
    uint8_t note;
} amy_voice_assignment_t;

#ifdef __cplusplus
extern "C" {
#endif

float amy_voice_map_role_amp(uint8_t role);
float amy_voice_map_main_pan(uint8_t role);
const char *amy_voice_map_role_name(uint8_t role);
const uint8_t *amy_voice_map_role_priority(amy_sound_backend_t backend);
void amy_voice_map_build_assignment(const synth_chord_t *chord,
                                    amy_voice_assignment_t assignments[AMY_MUSICAL_VOICE_COUNT]);
void amy_voice_map_append_note_log(char *buffer,
                                   size_t buffer_size,
                                   size_t *used,
                                   const char *prefix,
                                   uint8_t role,
                                   uint8_t note);

#ifdef __cplusplus
}
#endif

#endif
