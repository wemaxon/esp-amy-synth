#include "hil/hil_app.h"

#include "esp_heap_caps.h"
#include "hil_console.h"
#include "hmi/controls.h"
#include "synth/app.h"
#include "synth/drums.h"
#include "synth/looper.h"

static char s_text[3072];

static void put_app(hil_json_t *json, const synth_app_snapshot_t *app)
{
    hil_json_string(json, "mode", synth_play_mode_name(app->mode));
    hil_json_string(json, "key", synth_key_name(app->key));
    hil_json_string(json, "scale", synth_scale_name(app->scale));
    hil_json_int(json, "octave", app->global_octave);
    hil_json_array(json, "button_octaves");
    for (uint8_t i = 0; i < SYNTH_CHORD_BUTTON_COUNT; ++i) {
        hil_json_int(json, NULL, app->button_octave[i]);
    }
    hil_json_end(json);
    hil_json_uint(json, "held", app->held_mask);
    hil_json_uint(json, "focus", app->focus_slot + 1U);
    hil_json_string(json, "chord", app->chord_name);
    hil_json_array(json, "notes");
    for (uint8_t i = 0; app->focused_chord_valid && i < app->focused_chord.note_count; ++i) {
        hil_json_uint(json, NULL, app->focused_chord.notes[i]);
    }
    hil_json_end(json);
    hil_json_int(json, "bass_note",
                 app->focused_chord_valid && app->focused_chord.bass_enabled ? app->focused_chord.bass_note : -1);
    hil_json_string(json, "modifier", synth_chord_modifier_name(app->modifier));
    hil_json_string(json, "joystick", controls_joystick_direction_name(app->joystick_direction));
    hil_json_string(json, "joystick_mode", synth_joystick_mode_name(app->joystick_mode));
    hil_json_string(json, "effect", play_ui_effect_name(app->selected_setting));
    hil_json_bool(json, "voice_leading", app->voice_leading);
    hil_json_string(json, "bass_mode", synth_bass_mode_name(app->bass_mode));
    hil_json_string(json, "sound", synth_sound_name(app->sound));
    hil_json_uint(json, "bpm", app->bpm);
    hil_json_string(json, "status", app->status);
    hil_json_object(json, "beat");
    hil_json_bool(json, "running", app->beat.running);
    hil_json_string(json, "style", synth_drum_loop_style_name(app->beat.style));
    hil_json_string(json, "kit", synth_drum_kit_name(app->beat.kit));
    hil_json_uint(json, "origin", app->beat.origin_frame);
    hil_json_uint(json, "bpm", app->beat.bpm);
    hil_json_end(json);
}

static void put_looper(hil_json_t *json)
{
    synth_looper_snapshot_t looper = {0};
    synth_looper_get_snapshot(&looper);
    synth_looper_clock_t clock = {0};
    synth_looper_get_clock(&clock);
    hil_json_object(json, "looper");
    hil_json_uint(json, "frame", clock.frame);
    hil_json_uint(json, "sample_rate", clock.sample_rate_hz);
    hil_json_bool(json, "has_loop", clock.has_loop);
    hil_json_uint(json, "anchor", clock.loop_anchor_frame);
    hil_json_uint(json, "length", clock.loop_length_frames);
    hil_json_uint(json, "bars", looper.bar_count);
    hil_json_uint(json, "current", looper.current_track + 1U);
    hil_json_string(json, "message", looper.message);
    hil_json_array(json, "tracks");
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        hil_json_object(json, NULL);
        hil_json_string(json, "state", synth_looper_track_state_short_name(looper.track_state[i]));
        hil_json_uint(json, "ms", looper.track_ms[i]);
        hil_json_end(json);
    }
    hil_json_end(json);
    hil_json_end(json);
}

void hil_command_state(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    synth_app_snapshot_t app;
    if (synth_app_snapshot(&app, 1000) != ESP_OK) {
        hil_reply_error("app snapshot unavailable");
        return;
    }
    hil_json_t json;
    hil_json_init(&json, s_text, sizeof(s_text));
    hil_json_object(&json, NULL);
    put_app(&json, &app);
    put_looper(&json);
    hil_json_uint(&json, "app_stack_free", app.stack_free_bytes);
    hil_json_uint(&json, "heap_free", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    hil_json_end(&json);
    hil_reply_json(&json);
}
