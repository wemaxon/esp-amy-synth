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


def parse_scale_defs(chord_c: str) -> dict[str, list[int]]:
    scales: dict[str, list[int]] = {}
    pattern = re.compile(
        r"\[(SYNTH_SCALE_[A-Z_]+)\]\s*=\s*\{\"[^\"]+\",\s*(\d+),\s*\{([^}]+)\}\}"
    )
    for scale_name, note_count, interval_text in pattern.findall(chord_c):
        intervals = [int(value.strip()) for value in interval_text.split(",") if value.strip()]
        require(len(intervals) >= int(note_count), f"{scale_name} has too few scale intervals")
        scales[scale_name] = intervals[: int(note_count)]
    return scales


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
    scales = parse_scale_defs(chord_c)
    major = scales.get("SYNTH_SCALE_MAJOR")
    require(major is not None, "missing SYNTH_SCALE_MAJOR definition")

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
        "DOM7": [0, 4, 7, 10],
        "MAJ7": [0, 4, 7, 11],
        "MIN7": [0, 3, 7, 10],
        "MAJ9": [0, 4, 7, 11, 14],
        "MIN9": [0, 3, 7, 10, 14],
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


def assert_app_and_api_invariants(chord_c: str, chord_h: str, app_c: str) -> None:
    require("#define SYNTH_CHORD_INVERSION_COUNT 3" in chord_h, "inversion count must be 3")
    require("static const int8_t GLOBAL_OCTAVE_MIN = -1;" in app_c, "global octave min must be -1")
    require("static const int8_t GLOBAL_OCTAVE_MAX = 2;" in app_c, "global octave max must be +2")
    require("static const int8_t BUTTON_OCTAVE_MIN = -2;" in app_c, "button octave min must be -2")
    require("static const int8_t BUTTON_OCTAVE_MAX = 1;" in app_c, "button octave max must be +1")
    require("root - 24" in chord_c, "ROOT bass must be two octaves below the chord root")
    require("bass_degree" in chord_h and "bass_degree" in chord_c, "slash bass must use bass degree")
    require("slash_offset" not in chord_h + chord_c + app_c, "legacy slash offsets must not remain")
    require("SYNTH_CHORD_MOD_ROOT_DOWN" not in chord_h + chord_c, "root-down modifier must not remain")
    require("SYNTH_CHORD_MOD_ROOT_UP" not in chord_h + chord_c, "root-up modifier must not remain")
    require("request->modifier >= SYNTH_CHORD_MOD_COUNT" in chord_c, "modifier range check missing")
    require(
        "slot == s_app.focus_slot" in app_c and "locked_modifier[slot]" in app_c,
        "temporary and locked modifiers must be per-button",
    )


def main() -> int:
    chord_c = read(CHORD_C)
    chord_h = read(CHORD_H)
    app_c = read(APP_C)

    assert_joystick_maps(chord_c)
    assert_c_major_buttons(chord_c)
    assert_formula_arrays(chord_c)
    assert_formula_routes(chord_c)
    assert_app_and_api_invariants(chord_c, chord_h, app_c)

    print("PASS: chord engine matches checked HiChord manual invariants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
