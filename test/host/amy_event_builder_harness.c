#include <stdint.h>

#include "synth/amy_event_builder.c"

static inline __attribute__((always_inline)) amy_event build_raw_vector(void)
{
    const amy_sound_runtime_t runtime = {
        .backend = AMY_SOUND_BACKEND_PCM,
        .pcm_preset = 42,
        .pcm_loop = true,
        .velocity = 1.2f,
        .raw = {
            .wave = 7,
            .duty = 0.25f,
            .filter_cutoff_hz = 10000.0f,
            .resonance = 0.3f,
        },
    };
    const synth_amy_envelope_t envelope = {
        .attack_ms = 10,
        .decay_ms = 20,
        .sustain_level = 0.7f,
        .release_ms = 30,
    };
    const amy_raw_note_event_config_t config = {
        .amplitude = 0.5f,
        .pan = 1.5f,
        .detune_semitones = 0.25f,
        .tremolo_enabled = true,
        .lfo_enabled = true,
        .portamento_ms = 140,
        .filter = {
            .enabled = true,
            .cutoff_hz = 500.0f,
            .minimum_hz = 20.0f,
            .maximum_hz = 20000.0f,
        },
        .envelope = &envelope,
    };
    amy_event event;
    amy_event_build_raw_note(&event, 3, 60, &runtime, &config);
    return event;
}

int32_t host_vector_amy_raw_velocity(void)
{
    return (int32_t)(build_raw_vector().velocity * 1000.0f);
}

int32_t host_vector_amy_raw_note(void)
{
    return (int32_t)(build_raw_vector().midi_note * 100.0f);
}

int32_t host_vector_amy_raw_modulation(void)
{
    amy_event event = build_raw_vector();
    return (int32_t)(event.amp_coefs[COEF_EXT1] * 10.0f +
                     event.freq_coefs[COEF_EXT0]);
}

int32_t host_vector_amy_raw_filter(void)
{
    return (int32_t)build_raw_vector().filter_freq_coefs[COEF_CONST];
}

int32_t host_vector_amy_raw_envelope(void)
{
    amy_event event = build_raw_vector();
    return (int32_t)event.eg0_times[0] * 1000 +
           (int32_t)(event.eg0_values[1] * 1000.0f);
}

int32_t host_vector_amy_raw_pcm(void)
{
    amy_event event = build_raw_vector();
    return event.preset * 10 + (int32_t)event.feedback;
}

int32_t host_vector_amy_raw_pan(void)
{
    return (int32_t)(build_raw_vector().pan_coefs[COEF_CONST] * 1000.0f);
}

int32_t host_vector_amy_patch_default_velocity(void)
{
    amy_event event;
    amy_event_build_patch_note(&event, 1, 64.0f, NULL, 140);
    return (int32_t)(event.velocity * 1000.0f);
}

int32_t host_vector_amy_patch_portamento(void)
{
    amy_event event;
    amy_event_build_patch_note(&event, 1, 64.0f, NULL, 140);
    return event.portamento_ms;
}
