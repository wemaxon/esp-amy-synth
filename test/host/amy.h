#ifndef HOST_STUB_AMY_H
#define HOST_STUB_AMY_H

#include <stdint.h>

#define COEF_CONST 0
#define COEF_EXT0 1
#define COEF_EXT1 2
#define ENVELOPE_NORMAL 1
#define FILTER_NONE 0
#define FILTER_LPF 1

typedef struct {
    uint16_t osc;
    uint8_t wave;
    int16_t preset;
    float feedback;
    float midi_note;
    float velocity;
    float amp_coefs[4];
    float freq_coefs[4];
    uint16_t portamento_ms;
    float pan_coefs[4];
    float duty_coefs[4];
    uint8_t filter_type;
    float filter_freq_coefs[4];
    float resonance;
    uint8_t eg_type[2];
    uint32_t eg0_times[3];
    float eg0_values[3];
    uint8_t synth;
} amy_event;

static inline amy_event amy_default_event(void)
{
    return (amy_event){0};
}

#endif

