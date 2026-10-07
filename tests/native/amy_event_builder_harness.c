#include "test_check.h"
#include <stdint.h>

#include "synth/amy_event_builder.h"

static amy_event sent[16];
static unsigned sent_count;

void __wrap_amy_add_event(amy_event *event)
{
    sent[sent_count++] = *event;
    /* Model mutation by the real AMY enqueue operation. */
    event->velocity = -123.0f;
    event->portamento_ms = 999;
}

int32_t host_vector_amy_raw_dispatch(void)
{
    sent_count = 0;
    amy_event event = amy_default_event();
    event.portamento_ms = 140;
    event.velocity = 0.5f;
    amy_event_send_raw_control(&event, 2, 8, true);
    return sent_count == 2 && sent[0].osc == 2 && sent[1].osc == 8 &&
           sent[0].portamento_ms == 140 && sent[1].portamento_ms == 140 &&
           sent[1].velocity == 0.5f && event.velocity == 0.5f;
}

int32_t host_vector_amy_patch_dispatch(void)
{
    sent_count = 0;
    amy_event event = amy_default_event();
    event.portamento_ms = 60;
    amy_event_send_patch_control(&event, 3, true);
    return sent_count == 6 &&
           sent[0].synth == 0 && sent[0].osc == 0 &&
           sent[1].synth == 0 && sent[1].osc == 1 &&
           sent[2].synth == 0 && sent[2].osc == 2 &&
           sent[3].synth == 1 && sent[3].osc == 0 &&
           sent[4].synth == 1 && sent[4].osc == 1 &&
           sent[5].synth == 1 && sent[5].osc == 2 &&
           sent[5].portamento_ms == 60;
}

int32_t host_vector_amy_mono_dispatch(void)
{
    sent_count = 0;
    amy_event event = amy_default_event();
    amy_event_send_raw_control(&event, 5, 11, false);
    amy_event_send_patch_control(&event, 2, false);
    amy_event_send_patch_control(&event, 0, true);
    return sent_count == 3 && sent[0].osc == 5 &&
           sent[1].synth == 0 && sent[1].osc == 0 &&
           sent[2].synth == 0 && sent[2].osc == 1;
}

static amy_event build_raw_vector(void)
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

int32_t host_vector_amy_drum_hit_bus(void)
{
    amy_event event;
    amy_event_build_drum_hit(&event, 120, 6, 53, 0.5f, 0.25f, 137);
    return event.bus == AMY_DRUM_BUS && AMY_DRUM_BUS != AMY_SYNTH_BUS &&
           event.osc == 120 && event.wave == PCM && event.preset == 6 &&
           event.midi_note == 53.0f && event.velocity == 0.5f &&
           event.amp_coefs[COEF_CONST] == 0.25f && event.filter_type == FILTER_NONE &&
           event.pan_coefs[COEF_CONST] == 0.5f && event.sample_offset == 137;
}

int32_t host_vector_amy_volume_reaches_every_bus(void)
{
    sent_count = 0;
    amy_event_send_volume(4.0f);
    if (sent_count != AMY_BUS_COUNT) return 0;
    for (unsigned bus = 0; bus < AMY_BUS_COUNT; ++bus) {
        if (sent[bus].bus != bus || sent[bus].volume != 4.0f || AMY_IS_SET(sent[bus].osc)) return 0;
    }
    return 1;
}

int32_t host_vector_amy_synth_bus_reset(void)
{
    /* Clears a previous patch's EQ/chorus phrase from the synth bus only. */
    amy_event event;
    amy_event_build_synth_bus_reset(&event);
    return event.bus == AMY_SYNTH_BUS && event.eq_l == 0.0f && event.eq_m == 0.0f &&
           event.eq_h == 0.0f && event.chorus_level == 0.0f && AMY_IS_UNSET(event.osc) &&
           AMY_IS_UNSET(event.synth);
}

int main(void)
{
    CHECK_VECTOR(host_vector_amy_raw_dispatch, 1);
    CHECK_VECTOR(host_vector_amy_patch_dispatch, 1);
    CHECK_VECTOR(host_vector_amy_mono_dispatch, 1);
    CHECK_VECTOR(host_vector_amy_raw_velocity, 1000);
    CHECK_VECTOR(host_vector_amy_raw_note, 6025);
    CHECK_VECTOR(host_vector_amy_raw_modulation, 11);
    CHECK_VECTOR(host_vector_amy_raw_filter, 500);
    CHECK_VECTOR(host_vector_amy_raw_envelope, 10700);
    CHECK_VECTOR(host_vector_amy_raw_pcm, 421);
    CHECK_VECTOR(host_vector_amy_raw_pan, 1000);
    CHECK_VECTOR(host_vector_amy_patch_default_velocity, 550);
    CHECK_VECTOR(host_vector_amy_patch_portamento, 140);
    CHECK_VECTOR(host_vector_amy_drum_hit_bus, 1);
    CHECK_VECTOR(host_vector_amy_volume_reaches_every_bus, 1);
    CHECK_VECTOR(host_vector_amy_synth_bus_reset, 1);
    return 0;
}
