#!/usr/bin/env python3
"""Fast structural compatibility checks for behavior-preserving refactors.

These checks intentionally cover repository contracts that are easy to break
while moving code but do not require a target or hardware.  Behavioral vector
checks remain in ``chord_engine_spec_check.py``; target builds and hardware
smoke tests are separate gates.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def source(relative: str) -> str:
    path = ROOT / relative
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        fail(f"could not read {relative}: {exc}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_public_symbols(relative: str, symbols: tuple[str, ...]) -> None:
    header = source(relative)
    for symbol in symbols:
        require(re.search(rf"\b{re.escape(symbol)}\s*\(", header) is not None,
                f"{relative} lost public symbol {symbol}")


def check_public_facades() -> None:
    require_public_symbols("main/synth/app.h", ("synth_app_start",))
    require_public_symbols(
        "main/synth/amy_engine.h",
        (
            "synth_amy_init",
            "synth_amy_set_sound",
            "synth_amy_chord_on",
            "synth_amy_chord_off",
            "synth_amy_all_off",
        ),
    )
    require_public_symbols(
        "main/synth/looper.h",
        (
            "synth_looper_init",
            "synth_looper_process_audio",
            "synth_looper_click",
            "synth_looper_get_snapshot",
        ),
    )
    require_public_symbols(
        "main/hmi/controls.h",
        (
            "controls_init",
            "controls_update",
            "controls_pop_event",
            "controls_joystick_read",
            "controls_wheel_read",
        ),
    )


def check_timing_contracts() -> None:
    app = source("main/synth/app.c")
    require(
        re.search(r"APP_UPDATE_TICKS\s*=\s*pdMS_TO_TICKS\(20\)", app) is not None,
        "application update period changed from 20 ms",
    )
    require(
        re.search(r"SOUND_BROWSE_APPLY_TICKS\s*=\s*pdMS_TO_TICKS\(180\)", app)
        is not None,
        "sound browse apply delay changed from 180 ms",
    )


def check_persistence_contracts() -> None:
    expected = {
        "main/hmi/joystick_calibration.c": ("joy_cal", "data", "0x4a43414c", "1"),
        "main/hmi/wheel_calibration.c": ("wheel_cal", "data", "0x5743414c", "1"),
    }
    for relative, values in expected.items():
        text = source(relative)
        for value in values:
            require(value in text, f"{relative} persistence contract lost {value}")


def check_dependency_boundary() -> None:
    cmake = source("components/amy/CMakeLists.txt")
    require("amy/src" in cmake, "AMY component no longer points at the pinned source tree")
    require((ROOT / "components" / "amy" / "amy" / ".git").exists(),
            "AMY dependency is no longer a submodule checkout")


def check_board_contracts() -> None:
    expected_volumes = {
        "main/boards/ai_thinker_a1s.h": "100",
        "main/boards/wt0132p4_a1.h": "80",
    }
    for relative, volume in expected_volumes.items():
        text = source(relative)
        require(
            re.search(
                rf"ESP_POCKET_AUDIO_CODEC_DEFAULT_VOLUME_PERCENT\s+{volume}\b",
                text,
            )
            is not None,
            f"{relative} codec startup volume changed from {volume}%",
        )


def check_obsolete_test_removed() -> None:
    require(not (ROOT / "pytest_hello_world.py").exists(),
            "obsolete hello-world pytest returned")
    smoke = source("pytest_firmware.py")
    require("esp-pocket-audio synth app starting" in smoke,
            "firmware smoke test no longer checks the real entrypoint")


def main() -> None:
    check_public_facades()
    check_timing_contracts()
    check_persistence_contracts()
    check_dependency_boundary()
    check_board_contracts()
    check_obsolete_test_removed()
    print("PASS: firmware refactor compatibility contracts hold")


if __name__ == "__main__":
    main()
