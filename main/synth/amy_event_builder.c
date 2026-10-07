#include "synth/amy_event_builder.h"

#include <stddef.h>

void amy_event_send_raw_control(const amy_event *control, uint16_t main_osc,
                                 uint16_t pair_osc, bool pair_active)
{
    amy_event event = *control;
    event.osc = main_osc;
    amy_add_event(&event);
    if (pair_active) {
        /* AMY mutates queued events, including their time. Start afresh. */
        event = *control;
        event.osc = pair_osc;
        amy_add_event(&event);
    }
}

void amy_event_send_patch_control(const amy_event *control, uint8_t oscs_per_voice,
                                   bool pair_loaded)
{
    for (uint8_t pair = 0; pair < (pair_loaded ? 2U : 1U); ++pair) {
        for (uint8_t osc = 0; osc < oscs_per_voice; ++osc) {
            amy_event event = *control;
            event.synth = pair ? AMY_PATCH_PAIR_SYNTH : AMY_PATCH_MAIN_SYNTH;
            event.osc = osc;
            amy_add_event(&event);
        }
    }
}

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void amy_event_apply_envelope(amy_event *event, const synth_amy_envelope_t *envelope)
{
    if (event == NULL || envelope == NULL) {
        return;
    }

    event->eg_type[0] = ENVELOPE_NORMAL;
    event->eg0_times[0] = envelope->attack_ms;
    event->eg0_values[0] = 1.0f;
    event->eg0_times[1] = envelope->decay_ms;
    event->eg0_values[1] = envelope->sustain_level;
    event->eg0_times[2] = envelope->release_ms;
    event->eg0_values[2] = 0.0f;
}

void amy_event_apply_filter(amy_event *event,
                            const amy_raw_profile_t *profile,
                            const amy_filter_controls_t *controls)
{
    if (event == NULL || profile == NULL || controls == NULL) {
        return;
    }

    bool profile_filter = profile->filter_cutoff_hz < controls->maximum_hz;
    if (controls->enabled || profile_filter) {
        float cutoff = profile->filter_cutoff_hz;
        if (controls->enabled && controls->cutoff_hz < cutoff) {
            cutoff = controls->cutoff_hz;
        }
        event->filter_type = FILTER_LPF;
        event->filter_freq_coefs[COEF_CONST] =
            clamp_float(cutoff, controls->minimum_hz, controls->maximum_hz);
        event->resonance = controls->enabled ? 0.6f : profile->resonance;
    } else {
        event->filter_type = FILTER_NONE;
    }
}

void amy_event_build_raw_note(amy_event *event,
                              uint16_t osc,
                              uint8_t midi_note,
                              const amy_sound_runtime_t *runtime,
                              const amy_raw_note_event_config_t *config)
{
    if (event == NULL || runtime == NULL || config == NULL) {
        return;
    }

    amy_clear_event(event);
    const amy_raw_profile_t *profile = &runtime->raw;
    event->osc = osc;
    event->wave = profile->wave;
    if (runtime->backend == AMY_SOUND_BACKEND_PCM) {
        event->preset = runtime->pcm_preset;
        event->feedback = runtime->pcm_loop ? 1.0f : 0.0f;
    }
    event->midi_note = (float)midi_note + config->detune_semitones;
    event->velocity = clamp_float(runtime->velocity, 0.0f, 1.0f);
    event->amp_coefs[COEF_CONST] = config->amplitude;
    event->amp_coefs[COEF_EXT1] = config->tremolo_enabled ? 1.0f : 0.0f;
    event->freq_coefs[COEF_EXT0] = config->lfo_enabled ? 1.0f : 0.0f;
    event->portamento_ms = config->portamento_ms;
    event->pan_coefs[COEF_CONST] = clamp_float(config->pan, 0.0f, 1.0f);
    event->duty_coefs[COEF_CONST] = profile->duty;
    amy_event_apply_filter(event, profile, &config->filter);
    amy_event_apply_envelope(event, config->envelope);
}

void amy_event_build_patch_note(amy_event *event,
                                uint8_t synth_id,
                                float midi_note,
                                const amy_sound_runtime_t *runtime,
                                uint16_t portamento_ms)
{
    if (event == NULL) {
        return;
    }
    amy_clear_event(event);
    event->synth = synth_id;
    event->midi_note = midi_note;
    float velocity = runtime != NULL ? runtime->velocity : 0.55f;
    event->velocity = clamp_float(velocity, 0.0f, 1.0f);
    event->portamento_ms = portamento_ms;
}

void amy_event_build_raw_note_off(amy_event *event, uint16_t osc)
{
    if (event == NULL) {
        return;
    }
    amy_clear_event(event);
    event->osc = osc;
    event->velocity = 0.0f;
}

void amy_event_build_patch_note_off(amy_event *event, uint8_t synth_id, uint8_t midi_note)
{
    if (event == NULL) {
        return;
    }
    amy_clear_event(event);
    event->synth = synth_id;
    event->midi_note = midi_note;
    event->velocity = 0.0f;
}


void amy_event_build_drum_hit(amy_event *event,
                              uint16_t osc,
                              uint8_t pcm_preset,
                              uint8_t midi_note,
                              float velocity,
                              float amplitude,
                              uint16_t sample_offset)
{
    if (event == NULL) {
        return;
    }
    amy_clear_event(event);
    event->osc = osc;
    event->bus = AMY_DRUM_BUS;
    event->wave = PCM;
    event->preset = pcm_preset;
    event->midi_note = midi_note;
    event->velocity = velocity;
    event->amp_coefs[COEF_CONST] = amplitude;
    event->feedback = 0.0f;
    event->filter_type = FILTER_NONE;
    event->pan_coefs[COEF_CONST] = 0.5f;
    /* PCM note-on starts this many frames into the next rendered block. */
    event->sample_offset = sample_offset < AMY_BLOCK_SIZE ? sample_offset : AMY_BLOCK_SIZE - 1U;
}

void amy_event_send_volume(float volume)
{
    for (uint16_t bus = 0; bus < AMY_BUS_COUNT; ++bus) {
        amy_event event;
        amy_clear_event(&event);
        event.bus = bus;
        event.volume = volume;
        amy_add_event(&event);
    }
}

void amy_event_build_synth_bus_reset(amy_event *event)
{
    if (event == NULL) {
        return;
    }
    amy_clear_event(event);
    event->bus = AMY_SYNTH_BUS;
    event->eq_l = 0.0f;
    event->eq_m = 0.0f;
    event->eq_h = 0.0f;
    event->chorus_level = 0.0f;
}
