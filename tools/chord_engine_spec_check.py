#!/usr/bin/env python3
"""Run production chord vectors and retain firmware integration guards."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from c_source import function_body
from chord_engine_check import main as check_chords


ROOT = Path(__file__).resolve().parents[1]
CHORD_C = ROOT / "main" / "synth" / "chord_engine.c"
CHORD_H = ROOT / "main" / "synth" / "chord_engine.h"
APP_C = ROOT / "main" / "synth" / "app.c"
APP_CHORD_TOOLS_C = ROOT / "main" / "synth" / "app" / "chord_tools.c"
APP_EFFECTS_C = ROOT / "main" / "synth" / "app" / "effects.c"
APP_EFFECTS_H = ROOT / "main" / "synth" / "app" / "effects.h"
APP_KEY_C = ROOT / "main" / "synth" / "app" / "key.c"
APP_KEY_H = ROOT / "main" / "synth" / "app" / "key.h"
APP_SETTINGS_C = ROOT / "main" / "synth" / "app" / "settings.c"
AMY_C = ROOT / "main" / "synth" / "amy_engine.c"
AMY_H = ROOT / "main" / "synth" / "amy_engine.h"
AMY_EVENT_BUILDER_C = ROOT / "main" / "synth" / "amy_event_builder.c"
AMY_RUNTIME_C = ROOT / "main" / "synth" / "amy_runtime.c"
PLAY_UI_C = ROOT / "main" / "hmi" / "play_ui.c"
PLAY_UI_H = ROOT / "main" / "hmi" / "play_ui.h"

def die(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        die(f"could not read {path}: {exc}")


def require(condition: bool, message: str) -> None:
    if not condition:
        die(message)



def assert_app_and_api_invariants(
    chord_c: str,
    chord_h: str,
    app_c: str,
    app_chord_tools_c: str,
    app_key_c: str,
    app_key_h: str,
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    joystick_body = function_body(app_c, "update_joystick")
    idle_clear = re.search(
        r"if \(s_app(?:\.chords)?\.held_mask == 0\) \{\s*"
        r"s_app\.last_play_direction = joystick\.direction;\s*"
        r"if \(s_app\.modifier != SYNTH_CHORD_MOD_NONE\) \{\s*"
        r"s_app\.modifier = SYNTH_CHORD_MOD_NONE;",
        joystick_body,
    )
    idle_index = idle_clear.start() if idle_clear is not None else -1
    modifier_index = joystick_body.find("synth_chord_modifier_from_joystick")

    require("#define SYNTH_CHORD_INVERSION_COUNT 3" in chord_h, "inversion count must be 3")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_DEFAULT 0" in app_key_h, "global octave default must be 0")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_MIN -2" in app_key_h, "global octave min must be -2")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_MAX 2" in app_key_h, "global octave max must be +2")
    require("#define SYNTH_APP_BUTTON_OCTAVE_MIN -2" in app_key_h, "button octave min must be -2")
    require("#define SYNTH_APP_BUTTON_OCTAVE_MAX 2" in app_key_h, "button octave max must be +2")
    require("SYNTH_APP_GLOBAL_OCTAVE_DEFAULT" in app_c, "app must use shared global octave default")
    require("SYNTH_APP_GLOBAL_OCTAVE_DEFAULT" in play_ui_c, "play UI must use shared global octave default")
    require("bounce" not in app_c.lower(),
            "mode navigation must not bounce or replace the live drum transport")
    require("synth_app_drums_update(&s_app.drums, current_bpm(), xTaskGetTickCount(), &clock);" in app_c,
            "persistent drum transport must update independently of the selected UI mode")
    require(
        app_c.count("handle_mode_button_tap();") == 1
        and re.search(
            r"event->sw\s*==\s*CONTROL_MODE_MENU\)\s*\{\s*"
            r"handle_mode_button_tap\(\);",
            app_c,
        ) is not None,
        "Mode-button presses must retain tap-tempo behavior",
    )
    require("synth_app_key_step" in app_key_c and "--octave_value" in app_key_c and "++octave_value" in app_key_c,
            "key stepping must carry octave across B/C wraps")
    require("root - 24" in chord_c, "ROOT bass must be two octaves below the chord root")
    require("bass_degree" in chord_h and "bass_degree" in chord_c, "slash bass must use bass degree")
    require(".scale = s_app.scale" in app_c and "synth_scale_step" in app_c,
            "selected scale must reach the chord engine")
    require("PLAY_UI_EFFECT_SCALE" in play_ui_h and '"SCALE %s"' in play_ui_c,
            "scale must be a settings item after the effects (HiChord 2.9 menu order)")
    require("slash_offset" not in chord_h + chord_c + app_c, "legacy slash offsets must not remain")
    require("SYNTH_CHORD_MOD_ROOT_DOWN" not in chord_h + chord_c, "root-down modifier must not remain")
    require("SYNTH_CHORD_MOD_ROOT_UP" not in chord_h + chord_c, "root-up modifier must not remain")
    require("request->modifier >= SYNTH_CHORD_MOD_COUNT" in chord_c, "modifier range check missing")
    require(
        "slot == context->focus_slot" in app_chord_tools_c and "locked_modifier[slot]" in app_chord_tools_c,
        "temporary and locked modifiers must be per-button",
    )
    require(idle_index >= 0, "idle joystick path must clear transient modifiers")
    require(
        modifier_index >= 0 and idle_index < modifier_index,
        "idle joystick clear must run before chord modifier lookup",
    )


def assert_voice_count_control(
    app_c: str,
    app_settings_c: str,
    amy_c: str,
    amy_h: str,
    amy_runtime_c: str,
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    runtime_bodies = [
        function_body(amy_c, name)
        for name in ("configure_patch_synth_with_gain", "synth_amy_chord_on", "synth_amy_chord_voice_on")
    ]
    runtime_helper_body = function_body(amy_runtime_c, "amy_runtime_voice_limit")
    setter_body = function_body(amy_c, "synth_amy_set_voice_limit")
    toggle_body = function_body(app_c, "toggle_current_effect")

    require("esp_err_t synth_amy_set_voice_limit(uint8_t voices);" in amy_h, "voice-limit API missing")
    require("static uint8_t s_voice_limit = AMY_MUSICAL_VOICE_COUNT;" in amy_c, "voice-limit state missing")
    require(all("amy_runtime_voice_limit(runtime, s_voice_limit)" in body for body in runtime_bodies),
            "patch allocation and both note-on paths must honor the user voice cap")
    require("clamp_voice_count(requested_voice_limit)" in runtime_helper_body, "runtime voice helper must clamp user cap")
    require("synth_amy_musical_all_off()" in setter_body,
            "voice-limit changes must silence stale musical roles without resetting drums")
    require(
        "configure_patch_synth(amy_runtime_for_sound(s_sound))" in setter_body,
        "voice-limit changes must reconfigure loaded patch synths",
    )
    require(
        re.search(r"static\s+const\s+uint8_t\s+VOICE_LIMITS\[\]\s*=\s*\{\s*6,\s*4,\s*2,\s*1,\s*\};", app_settings_c, re.S)
        is not None,
        "app voice levels must map HiChord 8/4/2/1 to engine limits 6/4/2/1",
    )
    require(
        "synth_app_voice_limit(s_app.fx.voice_level)" in app_c and "synth_amy_set_voice_limit(voice_limit)" in app_c,
        "voice-count effect must call AMY voice limiter",
    )
    require("PLAY_UI_EFFECT_VOICE_COUNT" in toggle_body, "voice-count effect must be handled explicitly")
    require("PLAY_UI_EFFECT_VOICE_COUNT" in play_ui_h, "play UI effect enum missing voice count")
    require("uint8_t voice_level;" in play_ui_h, "play UI model missing voice level")
    require("PLAY_UI_EFFECT_VOICE_COUNT" in play_ui_c and '"VOICES %s"' in play_ui_c, "voice-count UI row missing")


def assert_adsr_control(
    app_c: str,
    app_effects_c: str,
    app_settings_c: str,
    amy_c: str,
    amy_h: str,
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    apply_wheel_body = function_body(app_c, "apply_wheel")
    wheel_target_body = function_body(app_c, "current_wheel_target")
    current_envelope_body = function_body(app_c, "current_envelope")
    runtime_controls_body = function_body(amy_c, "apply_patch_runtime_controls")
    setter_body = function_body(amy_c, "synth_amy_set_envelope")

    require("typedef struct {\n    uint32_t attack_ms;" in amy_h, "AMY envelope type missing")
    require("esp_err_t synth_amy_set_envelope(const synth_amy_envelope_t *envelope);" in amy_h, "AMY envelope API missing")
    for preset in (
        '{"LONG", {800, 1000, 0.70f, 2000}}',
        '{"SHORT", {100, 100, 1.00f, 120}}',
        '{"SWELL", {800, 300, 0.80f, 2000}}',
        '{"PLUCK", {5, 80, 0.00f, 180}}',
        '{"TOUCH", {25, 260, 0.66f, 450}}',
        '{"SUSTAIN", {200, 300, 0.85f, 3000}}',
    ):
        require(preset in app_settings_c, f"missing exact ADSR preset {preset}")
    require("PLAY_UI_EFFECT_ADSR" in play_ui_h and "uint32_t attack_ms;" in play_ui_h, "play UI ADSR model missing")
    require("PLAY_UI_EFFECT_ADSR" in play_ui_c and '"ADSR %s"' in play_ui_c, "ADSR UI row missing")
    require(
        "step_adsr_preset" in app_c and
        "synth_app_effects_step_adsr_preset(&s_app.fx)" in app_c and
        "synth_amy_set_envelope(&envelope)" in app_c,
        "ADSR preset cycling missing",
    )
    require("1UL << CONTROL_KEY_MENU" in wheel_target_body, "Gray+wheel attack control missing")
    require("synth_app_attack_ms_from_wheel" in apply_wheel_body, "attack wheel range missing")
    require("ATTACK_MIN_MS" in app_settings_c and "ATTACK_MAX_MS" in app_settings_c, "attack wheel range missing")
    require("1UL << CONTROL_SOUND_MENU" in wheel_target_body, "Yellow+wheel release control missing")
    require("synth_app_release_ms_from_wheel" in apply_wheel_body, "release wheel range missing")
    require("RELEASE_MIN_MS" in app_settings_c and "RELEASE_MAX_MS" in app_settings_c, "release wheel range missing")
    require("1UL << CONTROL_MODE_MENU" in wheel_target_body, "Red+wheel filter shortcut missing")
    require(
        "synth_app_effects_envelope(&s_app.fx" in current_envelope_body and
        "synth_app_adsr_repeat_release_cap_ms" in app_effects_c,
        "repeat mode must cap envelope release",
    )
    require("REPEAT_RELEASE_CAP_MS" in app_settings_c, "repeat mode must cap envelope release")
    require("apply_patch_envelope()" in runtime_controls_body, "patch synths must receive ADSR envelope")
    require("apply_raw_envelope(i)" in setter_body, "active raw voices must receive ADSR envelope")


def assert_glide_control(
    app_c: str,
    app_settings_c: str,
    amy_c: str,
    amy_h: str,
    amy_event_builder_c: str,
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    toggle_body = function_body(app_c, "toggle_current_effect")
    runtime_controls_body = function_body(amy_c, "apply_patch_runtime_controls")
    setter_body = function_body(amy_c, "synth_amy_set_portamento")
    raw_note_body = function_body(amy_c, "amy_note_on_raw_osc")
    patch_note_body = function_body(amy_c, "amy_note_on_patch_synth")
    raw_builder_body = function_body(amy_event_builder_c, "amy_event_build_raw_note")
    patch_builder_body = function_body(amy_event_builder_c, "amy_event_build_patch_note")

    require("esp_err_t synth_amy_set_portamento(uint16_t portamento_ms);" in amy_h, "AMY portamento API missing")
    require("static uint16_t s_portamento_ms;" in amy_c, "portamento state missing")
    require(
        re.search(r"static\s+const\s+uint16_t\s+GLIDE_LEVEL_MS\[\]\s*=\s*\{\s*0,\s*60,\s*140,\s*300,\s*\};", app_settings_c, re.S)
        is not None,
        "glide levels must map OFF/FAST/MED/SLOW to portamento ms",
    )
    require("PLAY_UI_EFFECT_GLIDE" in toggle_body and "step_glide_level()" in toggle_body, "glide effect toggle missing")
    require("synth_amy_set_portamento(current_glide_ms())" in app_c, "app must apply AMY portamento")
    require("PLAY_UI_EFFECT_GLIDE" in play_ui_h and "uint8_t glide_level;" in play_ui_h, "play UI glide model missing")
    require("PLAY_UI_EFFECT_GLIDE" in play_ui_c and '"GLIDE %s"' in play_ui_c, "glide UI row missing")
    require(".portamento_ms = s_portamento_ms" in raw_note_body and
            "event->portamento_ms = config->portamento_ms" in raw_builder_body,
            "raw note-on must carry portamento")
    require("s_portamento_ms" in patch_note_body and
            "event->portamento_ms = portamento_ms" in patch_builder_body,
            "patch note-on must carry portamento")
    require("apply_patch_portamento()" in runtime_controls_body, "patch synths must receive portamento setting")
    require("apply_raw_portamento(i)" in setter_body, "active raw voices must receive portamento setting")


def main() -> int:
    chord_c = read(CHORD_C)
    chord_h = read(CHORD_H)
    app_c = read(APP_C) + "\n".join(read(path) for path in sorted((APP_C.parent / "app").glob("*.c")))
    app_chord_tools_c = read(APP_CHORD_TOOLS_C)
    app_effects_c = read(APP_EFFECTS_C)
    app_effects_h = read(APP_EFFECTS_H)
    app_key_c = read(APP_KEY_C)
    app_key_h = read(APP_KEY_H)
    app_settings_c = read(APP_SETTINGS_C)
    amy_c = read(AMY_C)
    amy_h = read(AMY_H)
    amy_event_builder_c = read(AMY_EVENT_BUILDER_C)
    amy_runtime_c = read(AMY_RUNTIME_C)
    play_ui_c = read(PLAY_UI_C)
    play_ui_h = read(PLAY_UI_H)

    assert_app_and_api_invariants(chord_c,
                                  chord_h,
                                  app_c,
                                  app_chord_tools_c,
                                  app_key_c,
                                  app_key_h,
                                  play_ui_c,
                                  play_ui_h)
    assert_voice_count_control(app_c, app_settings_c, amy_c, amy_h, amy_runtime_c, play_ui_c, play_ui_h)
    require("typedef struct {" in app_effects_h and "synth_app_effects_t" in app_effects_h,
            "app effects state module missing")
    assert_adsr_control(app_c, app_effects_c, app_settings_c, amy_c, amy_h, play_ui_c, play_ui_h)
    assert_glide_control(app_c,
                         app_settings_c,
                         amy_c,
                         amy_h,
                         amy_event_builder_c,
                         play_ui_c,
                         play_ui_h)

    check_chords()
    print("PASS: chord behavior vectors and firmware integration guards hold")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
