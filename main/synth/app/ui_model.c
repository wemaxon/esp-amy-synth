#include "synth/app/ui_model.h"

#include <string.h>

#include "synth/looper.h"

static bool any_chord_locked(const synth_app_chord_state_t *chords)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if (chords->locked_modifier[slot] != SYNTH_CHORD_MOD_NONE) {
            return true;
        }
    }
    return false;
}

void synth_app_ui_render(const synth_app_state_t *state, play_ui_screen_t screen)
{
    if (state == NULL) {
        return;
    }

    play_ui_model_t model = {0};
    play_ui_model_init(&model);
    model.screen = screen;
    model.key = state->key;
    model.global_octave_shift = state->global_octave_shift;
    memcpy(model.button_octave_shift,
           state->button_octave_shift,
           sizeof(model.button_octave_shift));
    model.modifier = state->modifier;
    model.joystick_mode = state->joystick_mode;
    model.bass_mode = state->bass_mode;
    model.voice_leading_enabled = state->voice_leading_enabled;
    model.chord_lock_enabled = any_chord_locked(&state->chords);
    model.sound = synth_app_sound_current(&state->sound);
    model.mode = state->mode;
    model.bpm = synth_app_tempo_bpm(&state->tempo);
    model.mode_bpm_submenu = state->mode_bpm_submenu;
    model.strum_speed = state->play.strum_speed;
    model.arp_pattern = state->play.arp_pattern;
    model.arp_rate = state->play.arp_rate;
    model.arp_chord_mode = state->play.arp_chord_mode;
    model.repeat_rate = state->play.repeat_rate;
    model.drum_kit = state->drums.loop.kit;
    model.drum_loop_style = state->drums.loop.style;
    model.drum_loop_variation = state->drums.loop.variation;
    model.drum_loop_running = synth_app_drums_loop_active(&state->drums);
    strlcpy(model.drum_auto_rate,
            synth_app_drums_auto_rate_name(&state->drums),
            sizeof(model.drum_auto_rate));
    model.held_mask = state->chords.held_mask;
    model.focus_slot = state->chords.focus_slot;
    model.effect_index = state->fx.effect_index;
    memcpy(model.effects, state->fx.enabled, sizeof(model.effects));
    model.lfo_level = state->fx.lfo_level;
    model.delay_level = state->fx.delay_level;
    model.tremolo_level = state->fx.tremolo_level;
    model.voice_level = state->fx.voice_level;
    model.adsr_preset = state->fx.adsr_preset;
    model.attack_ms = state->fx.attack_ms;
    model.release_ms = state->fx.release_ms;
    model.glide_level = state->fx.glide_level;
    model.wheel_present = synth_app_wheel_present(&state->wheel);
    model.wheel_percent = synth_app_wheel_percent(&state->wheel);
    model.volume_percent = synth_app_wheel_volume_percent(&state->wheel);
    model.joystick_direction = state->joystick_direction;
    synth_looper_get_snapshot(&model.looper);
    strlcpy(model.chord_name, state->chord_name, sizeof(model.chord_name));
    strlcpy(model.status, state->status, sizeof(model.status));
    play_ui_update(&model);
}

