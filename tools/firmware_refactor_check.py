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
            "synth_looper_get_diagnostics",
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
    require('"${AMY_SRC_DIR}/cv_trigger.c"' in cmake,
            "AMY 1.2.152 cv_trigger source is missing from the component")
    require((ROOT / "components" / "amy" / "amy" / ".git").exists(),
            "AMY dependency is no longer a submodule checkout")


def check_amy_upgrade_contracts() -> None:
    properties = source("components/amy/amy/library.properties")
    engine = source("main/synth/amy_engine.c")
    perf = source("main/synth/amy_perf.c")
    amy_midi = source("components/amy/amy/src/amy_midi.c")
    amy_i2s = source("components/amy/amy/src/i2s.c")
    amy_patches = source("components/amy/amy/src/patches.c")

    require(re.search(r"^version=1\.2\.152$", properties, re.MULTILINE) is not None,
            "AMY dependency is no longer version 1.2.152")
    require("event.voices" not in engine,
            "AMY adapter reintroduced the removed direct event voice API")
    require("amy_global.echo" not in engine,
            "AMY adapter reintroduced the removed global echo state")
    require("synth[osc]->role == SYNTH_IS_MOD_SOURCE" in perf and
            "case SYNTH_INAUDIBLE:" in perf,
            "AMY diagnostics no longer honor the separate role/status model")
    require(re.search(r"void\s+run_midi_task\s*\(\s*void\s*\*", amy_midi) is not None,
            "AMY MIDI task lost its ESP-IDF TaskFunction_t-compatible signature")
    require(re.search(r"void\s+esp_fill_audio_buffer_task\s*\(\s*void\s*\*", amy_i2s)
            is not None,
            "AMY audio task lost its ESP-IDF TaskFunction_t-compatible signature")
    require("synth[i] != NULL && synth[i]->status == SYNTH_AUDIBLE" in amy_patches,
            "AMY all-notes-off path lost its reserved-osc null guard")


def check_board_contracts() -> None:
    expected_volumes = {
        "main/boards/ai_thinker_a1s.h": "100",
        "main/boards/wt0132p4_a1.h": "100",
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


def check_looper_realtime_boundary() -> None:
    looper = source("main/synth/looper.c")
    looper_dsp = source("main/synth/looper_dsp.c")
    transport = source("main/synth/looper_transport.h")
    app = source("main/synth/app.c")
    audio = source("main/synth/amy_audio.c")
    drum_transport = source("main/synth/app/drum_transport.c")
    cmake = source("main/CMakeLists.txt")

    require("portENTER_CRITICAL" not in looper and "portEXIT_CRITICAL" not in looper,
            "looper reintroduced a shared critical section")
    require(re.search(r"\b\w+_locked\b", looper) is None,
            "looper retained a stale lock-coupled helper")
    require("#define SYNTH_LOOPER_COMMAND_CAPACITY 32U" in transport,
            "looper command queue is no longer the reviewed 32-entry bound")
    require("#define SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT 3U" in transport,
            "looper snapshot exchange is no longer triple buffered")
    require('"synth/looper_transport.c"' in cmake,
            "looper transport is missing from the firmware component")
    require('"synth/app/drum_transport.c"' in cmake and
            "synth_app_drum_transport_update" in drum_transport,
            "persistent drum transport is missing from the application")
    require("bounce" not in app.lower() and
            "bounce" not in looper.lower() and
            "bounce" not in source("main/synth/looper.h").lower() and
            "bounce" not in transport.lower(),
            "deleted automatic bounce path returned")
    require("heap_caps_malloc(buffer_bytes, MALLOC_CAP_8BIT)" not in looper,
            "looper storage reintroduced an internal-RAM fallback")
    require("bpm == 0 ? 120" not in looper,
            "looper reintroduced implicit BPM substitution")
    require("synth_looper_record_process_time" in audio,
            "audio callback no longer records looper deadline telemetry")
    require(re.search(r"LOOP_TRACK_DEFAULT_VOLUME_PERCENT\s*=\s*100", looper) is not None and
            re.search(r"LOOP_BUS_GAIN_BY_TRACKS_Q8[^=]*=\s*\{\s*0,\s*256,", looper_dsp, re.S)
            is not None,
            "single-track looper playback is no longer unity gain")
    require("LIVE_MIX_GAIN" not in looper and "synth_looper_dsp_soft_limit" not in looper,
            "looper reintroduced hidden live attenuation or nonlinear limiting")
    require("synth_looper_dsp_condition_seam" in looper and "crossfade" not in looper.lower(),
            "looper seam is no longer conditioned once at recording finalization")
    allocation = looper.find("esp_err_t allocation_err = allocate_all_track_buffers()")
    initialized = looper.find("s_initialized = true", allocation)
    require(allocation >= 0 and initialized > allocation,
            "looper exposes initialized state before track preallocation succeeds")


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
    check_amy_upgrade_contracts()
    check_board_contracts()
    check_looper_realtime_boundary()
    check_obsolete_test_removed()
    print("PASS: firmware refactor compatibility contracts hold")


if __name__ == "__main__":
    main()
