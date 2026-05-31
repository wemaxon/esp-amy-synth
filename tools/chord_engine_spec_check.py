#!/usr/bin/env python3
"""Validate HiChord manual invariants against the chord engine source."""

from __future__ import annotations

import re
import sys
from pathlib import Path


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
AMY_RUNTIME_C = ROOT / "main" / "synth" / "amy_runtime.c"
AMY_VOICE_MAP_C = ROOT / "main" / "synth" / "amy_voice_map.c"
PLAY_UI_C = ROOT / "main" / "hmi" / "play_ui.c"
PLAY_UI_H = ROOT / "main" / "hmi" / "play_ui.h"

KEY_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


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


def function_body(source: str, name: str) -> str:
    marker = name + "("
    start = source.find(marker)
    if start < 0:
        die(f"missing function {name}")
    brace = source.find("{", start)
    if brace < 0:
        die(f"missing body for {name}")

    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    die(f"unterminated function {name}")


def case_body(source: str, case_name: str) -> str:
    marker = f"case {case_name}:"
    start = source.find(marker)
    if start < 0:
        die(f"missing switch case {case_name}")
    next_case = source.find("case ", start + len(marker))
    default_case = source.find("default:", start + len(marker))
    candidates = [index for index in (next_case, default_case) if index >= 0]
    end = min(candidates) if candidates else len(source)
    return source[start:end]


def switch_returns(source: str, function_name: str) -> dict[str, str]:
    body = function_body(source, function_name)
    return dict(
        re.findall(
            r"case\s+(CONTROL_JOYSTICK_[A-Z_]+):\s*return\s+(SYNTH_CHORD_MOD_[A-Z0-9_]+);",
            body,
        )
    )


def assert_joystick_maps(chord_c: str) -> None:
    expected = {
        "default_modifier_from_joystick": {
            "CONTROL_JOYSTICK_UP": "SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR",
            "CONTROL_JOYSTICK_UP_RIGHT": "SYNTH_CHORD_MOD_DOM7",
            "CONTROL_JOYSTICK_RIGHT": "SYNTH_CHORD_MOD_MAJ7_MIN7",
            "CONTROL_JOYSTICK_DOWN_RIGHT": "SYNTH_CHORD_MOD_ADD9",
            "CONTROL_JOYSTICK_DOWN": "SYNTH_CHORD_MOD_SUS4",
            "CONTROL_JOYSTICK_DOWN_LEFT": "SYNTH_CHORD_MOD_SIXTH_OR_SUS2",
            "CONTROL_JOYSTICK_LEFT": "SYNTH_CHORD_MOD_DIM_OR_MINOR",
            "CONTROL_JOYSTICK_UP_LEFT": "SYNTH_CHORD_MOD_AUGMENTED",
        },
        "extended_modifier_from_joystick": {
            "CONTROL_JOYSTICK_UP": "SYNTH_CHORD_MOD_FLIP_MAJOR_MINOR",
            "CONTROL_JOYSTICK_UP_RIGHT": "SYNTH_CHORD_MOD_DOM9",
            "CONTROL_JOYSTICK_RIGHT": "SYNTH_CHORD_MOD_ADD11",
            "CONTROL_JOYSTICK_DOWN_RIGHT": "SYNTH_CHORD_MOD_MIN11",
            "CONTROL_JOYSTICK_DOWN": "SYNTH_CHORD_MOD_DOM7_SHARP9",
            "CONTROL_JOYSTICK_DOWN_LEFT": "SYNTH_CHORD_MOD_ADD9",
            "CONTROL_JOYSTICK_LEFT": "SYNTH_CHORD_MOD_SUS4_FLAT7",
            "CONTROL_JOYSTICK_UP_LEFT": "SYNTH_CHORD_MOD_HALF_DIM7",
        },
        "chromatic_modifier_from_joystick": {
            "CONTROL_JOYSTICK_UP": "SYNTH_CHORD_MOD_MIN_MAJ7",
            "CONTROL_JOYSTICK_UP_RIGHT": "SYNTH_CHORD_MOD_DOM13",
            "CONTROL_JOYSTICK_RIGHT": "SYNTH_CHORD_MOD_SIX_NINE",
            "CONTROL_JOYSTICK_DOWN_RIGHT": "SYNTH_CHORD_MOD_DOM7_ALT",
            "CONTROL_JOYSTICK_DOWN": "SYNTH_CHORD_MOD_MAJ13",
            "CONTROL_JOYSTICK_DOWN_LEFT": "SYNTH_CHORD_MOD_DOM7_FLAT9",
            "CONTROL_JOYSTICK_LEFT": "SYNTH_CHORD_MOD_HALF_DIM7",
            "CONTROL_JOYSTICK_UP_LEFT": "SYNTH_CHORD_MOD_MAJ7_SHARP11",
        },
    }

    for function_name, expected_map in expected.items():
        actual = switch_returns(chord_c, function_name)
        for direction, modifier in expected_map.items():
            require(
                actual.get(direction) == modifier,
                f"{function_name} {direction}: expected {modifier}, got {actual.get(direction)}",
            )


def parse_diatonic_major_scale(chord_c: str) -> list[int]:
    match = re.search(
        r"DIATONIC_MAJOR_SCALE\s*=\s*\{\s*(\d+),\s*\{([^}]+)\}\s*\};",
        chord_c,
    )
    require(match is not None, "missing fixed diatonic major scale definition")
    note_count = int(match.group(1))
    intervals = [int(value.strip()) for value in match.group(2).split(",") if value.strip()]
    require(len(intervals) >= note_count, "fixed diatonic major scale has too few intervals")
    return intervals[:note_count]


def scale_degree_offset(intervals: list[int], degree_index: int) -> int:
    octave = 0
    note_count = len(intervals)
    while degree_index < 0:
        degree_index += note_count
        octave -= 1
    octave += degree_index // note_count
    return intervals[degree_index % note_count] + octave * 12


def classify_suffix(third: int, fifth: int) -> str:
    third %= 12
    fifth %= 12
    if third == 4 and fifth == 7:
        return ""
    if third == 3 and fifth == 7:
        return "m"
    if third == 3 and fifth == 6:
        return "dim"
    if third == 4 and fifth == 8:
        return "aug"
    if third == 2 and fifth == 7:
        return "sus2"
    if third == 5 and fifth == 7:
        return "sus4"
    return "sc"


def assert_c_major_buttons(chord_c: str) -> None:
    major = parse_diatonic_major_scale(chord_c)

    expected = [
        ("C", [60, 64, 67]),
        ("Dm", [62, 65, 69]),
        ("Em", [64, 67, 71]),
        ("F", [65, 69, 72]),
        ("G", [67, 71, 74]),
        ("Am", [69, 72, 76]),
        ("Bdim", [71, 74, 77]),
    ]

    for degree, (expected_name, expected_notes) in enumerate(expected, start=1):
        root_degree = degree - 1
        root = 60 + scale_degree_offset(major, root_degree)
        intervals = [
            0,
            scale_degree_offset(major, root_degree + 2) - scale_degree_offset(major, root_degree),
            scale_degree_offset(major, root_degree + 4) - scale_degree_offset(major, root_degree),
        ]
        notes = [root + interval for interval in intervals]
        suffix = classify_suffix(intervals[1], intervals[2])
        name = KEY_NAMES[root % 12] + suffix
        require(name == expected_name, f"degree {degree} name: expected {expected_name}, got {name}")
        require(notes == expected_notes, f"degree {degree} notes: expected {expected_notes}, got {notes}")


def parse_interval_arrays(chord_c: str) -> dict[str, list[int]]:
    arrays: dict[str, list[int]] = {}
    pattern = re.compile(r"static\s+const\s+int16_t\s+([A-Z0-9_]+)\[\]\s*=\s*\{([^}]+)\};")
    for name, values in pattern.findall(chord_c):
        arrays[name] = [int(value.strip()) for value in values.split(",") if value.strip()]
    return arrays


def assert_formula_arrays(chord_c: str) -> None:
    arrays = parse_interval_arrays(chord_c)
    expected = {
        "MINOR_TRIAD": [0, 3, 7],
        "DIM_TRIAD": [0, 3, 6],
        "DOM7": [0, 4, 7, 10],
        "MAJ7": [0, 4, 7, 11],
        "MIN7": [0, 3, 7, 10],
        "ADD9_MAJOR": [0, 4, 7, 14],
        "SUS4": [0, 5, 7],
        "MAJOR6": [0, 4, 7, 9],
        "SUS2": [0, 2, 7],
        "DOM7_SHARP9": [0, 4, 7, 10, 15],
        "SUS4_FLAT7": [0, 5, 7, 10],
        "DOM9": [0, 4, 7, 10, 14],
        "MIN11": [0, 3, 7, 10, 17],
        "MIN_MAJ7": [0, 3, 7, 11],
        "MAJ13": [0, 4, 7, 11, 14, 21],
        "SIX_NINE": [0, 4, 7, 9, 14],
        "MAJ7_SHARP11": [0, 4, 7, 11, 18],
        "DOM13": [0, 4, 7, 10, 14, 21],
        "DOM7_FLAT9": [0, 4, 7, 10, 13],
        "DOM7_ALT": [0, 4, 6, 10, 13, 15],
    }
    for name, values in expected.items():
        require(arrays.get(name) == values, f"{name}: expected {values}, got {arrays.get(name)}")


def assert_formula_routes(chord_c: str) -> None:
    body = function_body(chord_c, "formula_for_modifier")
    expected = {
        "SYNTH_CHORD_MOD_DOM7_SHARP9": ("DOM7_SHARP9", "7#9"),
        "SYNTH_CHORD_MOD_SUS4_FLAT7": ("SUS4_FLAT7", "7sus4"),
        "SYNTH_CHORD_MOD_DOM9": ("DOM9", "9"),
        "SYNTH_CHORD_MOD_MIN11": ("MIN11", "m11"),
        "SYNTH_CHORD_MOD_MIN_MAJ7": ("MIN_MAJ7", "mMaj7"),
        "SYNTH_CHORD_MOD_MAJ13": ("MAJ13", "maj13"),
        "SYNTH_CHORD_MOD_SIX_NINE": ("SIX_NINE", "6/9"),
        "SYNTH_CHORD_MOD_MAJ7_SHARP11": ("MAJ7_SHARP11", "maj7#11"),
        "SYNTH_CHORD_MOD_DOM13": ("DOM13", "13"),
        "SYNTH_CHORD_MOD_DOM7_FLAT9": ("DOM7_FLAT9", "7b9"),
        "SYNTH_CHORD_MOD_DOM7_ALT": ("DOM7_ALT", "7alt"),
    }
    for modifier, (array_name, suffix) in expected.items():
        pattern = (
            rf"case\s+{modifier}:.*?"
            rf"formula_set\(&formula,\s*{array_name},\s*\d+,\s*\"{re.escape(suffix)}\""
        )
        require(re.search(pattern, body, re.S) is not None, f"{modifier} does not route to {array_name}")

    dim_or_minor = case_body(body, "SYNTH_CHORD_MOD_DIM_OR_MINOR")
    require(
        "base_is_majorish(base_formula->base)" in dim_or_minor,
        "default LEFT must branch on major/minor chord quality",
    )
    require("MINOR_TRIAD" in dim_or_minor, "default LEFT must turn major chords minor")
    require("DIM_TRIAD" in dim_or_minor, "default LEFT must turn minor chords diminished")


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
    combined_public_sources = chord_h + chord_c + app_c + play_ui_c + play_ui_h

    require("#define SYNTH_CHORD_INVERSION_COUNT 3" in chord_h, "inversion count must be 3")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_DEFAULT 0" in app_key_h, "global octave default must be 0")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_MIN -1" in app_key_h, "global octave min must be -1")
    require("#define SYNTH_APP_GLOBAL_OCTAVE_MAX 2" in app_key_h, "global octave max must be +2")
    require("#define SYNTH_APP_BUTTON_OCTAVE_MIN -2" in app_key_h, "button octave min must be -2")
    require("#define SYNTH_APP_BUTTON_OCTAVE_MAX 1" in app_key_h, "button octave max must be +1")
    require("SYNTH_APP_GLOBAL_OCTAVE_DEFAULT" in app_c, "app must use shared global octave default")
    require("SYNTH_APP_GLOBAL_OCTAVE_DEFAULT" in play_ui_c, "play UI must use shared global octave default")
    require("synth_app_key_step" in app_key_c and "--octave_value" in app_key_c and "++octave_value" in app_key_c,
            "key stepping must carry octave across B/C wraps")
    require("root - 24" in chord_c, "ROOT bass must be two octaves below the chord root")
    require("bass_degree" in chord_h and "bass_degree" in chord_c, "slash bass must use bass degree")
    require(
        "SYNTH_SCALE" not in combined_public_sources and "synth_scale_" not in combined_public_sources,
        "legacy scale-selection API/UI must not remain",
    )
    require("PLAY_UI_EFFECT_SCALE" not in play_ui_h + play_ui_c + app_c, "scale must not be an effect item")
    require('"SCALE %s"' not in play_ui_c, "scale must not be displayed as a user setting")
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


def assert_amy_voice_architecture(amy_voice_map_c: str) -> None:
    body = function_body(amy_voice_map_c, "amy_voice_map_build_assignment")
    require(
        re.search(
            r"PATCH_ROLE_PRIORITY\[AMY_MUSICAL_VOICE_COUNT\]\s*=\s*\{\s*0,\s*1,\s*2,\s*3,\s*4,\s*5,\s*\};",
            amy_voice_map_c,
            re.S,
        )
        is not None,
        "voice role priority must stay in role order for voice-count limiting",
    )
    require("assign_voice_note(assignments, 3, chord->bass_note)" in body, "bass must map to voice role 4")
    require(
        "assign_voice_note(assignments, 3, chord_fill_note(chord, 3))" in body,
        "bass-off chords must reuse voice role 4 as chord fill",
    )
    require(
        "assign_voice_note(assignments, 5, chord_fill_note(chord, 5))" in body,
        "bass-off chords must fill through voice role 6",
    )
    require("if (chord->note_count == 1)" in body, "one-note lead/arp assignments must stay monophonic")
    require("if (chord->note_count == 2)" in body, "two-note arp assignments must stay two-voice")
    require("octave_fill_note" in amy_voice_map_c, "fill voices must use octave chord-tone doubling")


def assert_voice_count_control(
    app_c: str,
    app_settings_c: str,
    amy_c: str,
    amy_h: str,
    amy_runtime_c: str,
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    runtime_body = function_body(amy_c, "runtime_voice_limit")
    runtime_helper_body = function_body(amy_runtime_c, "amy_runtime_voice_limit")
    setter_body = function_body(amy_c, "synth_amy_set_voice_limit")
    toggle_body = function_body(app_c, "toggle_current_effect")

    require("esp_err_t synth_amy_set_voice_limit(uint8_t voices);" in amy_h, "voice-limit API missing")
    require("static uint8_t s_voice_limit = AMY_MUSICAL_VOICE_COUNT;" in amy_c, "voice-limit state missing")
    require("amy_runtime_voice_limit(runtime, s_voice_limit)" in runtime_body, "runtime voice limit must honor user cap")
    require("clamp_voice_count(requested_voice_limit)" in runtime_helper_body, "runtime voice helper must clamp user cap")
    require("synth_amy_all_off()" in setter_body, "voice-limit changes must silence stale roles")
    require(
        "configure_patch_synth(runtime_for_sound(s_sound))" in setter_body,
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
    require("controls_switch_is_pressed(CONTROL_KEY_MENU)" in wheel_target_body, "Gray+wheel attack control missing")
    require("synth_app_attack_ms_from_wheel" in apply_wheel_body, "attack wheel range missing")
    require("ATTACK_MIN_MS" in app_settings_c and "ATTACK_MAX_MS" in app_settings_c, "attack wheel range missing")
    require("controls_switch_is_pressed(CONTROL_SOUND_MENU)" in wheel_target_body, "Yellow+wheel release control missing")
    require("synth_app_release_ms_from_wheel" in apply_wheel_body, "release wheel range missing")
    require("RELEASE_MIN_MS" in app_settings_c and "RELEASE_MAX_MS" in app_settings_c, "release wheel range missing")
    require("controls_switch_is_pressed(CONTROL_MODE_MENU)" in wheel_target_body, "Red+wheel filter shortcut missing")
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
    play_ui_c: str,
    play_ui_h: str,
) -> None:
    toggle_body = function_body(app_c, "toggle_current_effect")
    runtime_controls_body = function_body(amy_c, "apply_patch_runtime_controls")
    setter_body = function_body(amy_c, "synth_amy_set_portamento")
    raw_note_body = function_body(amy_c, "amy_note_on_raw_osc")
    patch_note_body = function_body(amy_c, "amy_note_on_patch_synth")

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
    require("event.portamento_ms = s_portamento_ms" in raw_note_body, "raw note-on must carry portamento")
    require("event.portamento_ms = s_portamento_ms" in patch_note_body, "patch note-on must carry portamento")
    require("apply_patch_portamento()" in runtime_controls_body, "patch synths must receive portamento setting")
    require("apply_raw_portamento(i)" in setter_body, "active raw voices must receive portamento setting")


def main() -> int:
    chord_c = read(CHORD_C)
    chord_h = read(CHORD_H)
    app_c = read(APP_C)
    app_chord_tools_c = read(APP_CHORD_TOOLS_C)
    app_effects_c = read(APP_EFFECTS_C)
    app_effects_h = read(APP_EFFECTS_H)
    app_key_c = read(APP_KEY_C)
    app_key_h = read(APP_KEY_H)
    app_settings_c = read(APP_SETTINGS_C)
    amy_c = read(AMY_C)
    amy_h = read(AMY_H)
    amy_runtime_c = read(AMY_RUNTIME_C)
    amy_voice_map_c = read(AMY_VOICE_MAP_C)
    play_ui_c = read(PLAY_UI_C)
    play_ui_h = read(PLAY_UI_H)

    assert_joystick_maps(chord_c)
    assert_c_major_buttons(chord_c)
    assert_formula_arrays(chord_c)
    assert_formula_routes(chord_c)
    assert_app_and_api_invariants(chord_c,
                                  chord_h,
                                  app_c,
                                  app_chord_tools_c,
                                  app_key_c,
                                  app_key_h,
                                  play_ui_c,
                                  play_ui_h)
    assert_amy_voice_architecture(amy_voice_map_c)
    assert_voice_count_control(app_c, app_settings_c, amy_c, amy_h, amy_runtime_c, play_ui_c, play_ui_h)
    require("typedef struct {" in app_effects_h and "synth_app_effects_t" in app_effects_h,
            "app effects state module missing")
    assert_adsr_control(app_c, app_effects_c, app_settings_c, amy_c, amy_h, play_ui_c, play_ui_h)
    assert_glide_control(app_c, app_settings_c, amy_c, amy_h, play_ui_c, play_ui_h)

    print("PASS: chord engine matches checked HiChord manual invariants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
