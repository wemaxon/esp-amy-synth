#ifndef HOST_STUB_SYNTH_AMY_ENGINE_H
#define HOST_STUB_SYNTH_AMY_ENGINE_H

#include <stdint.h>
#include "esp_err.h"
#include "synth/chord_engine.h"

esp_err_t synth_amy_install_mic_sample(const int16_t *samples, uint32_t frames, float reference_note);
esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord);
esp_err_t synth_amy_chord_off(uint8_t slot);
esp_err_t synth_amy_chord_voice_on(uint8_t slot, const synth_chord_t *chord, uint8_t voice);
esp_err_t synth_amy_musical_all_off(void);

typedef struct {
    uint32_t attack_ms;
    uint32_t decay_ms;
    float sustain_level;
    uint32_t release_ms;
} synth_amy_envelope_t;

#endif

