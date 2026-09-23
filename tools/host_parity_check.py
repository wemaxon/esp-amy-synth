#!/usr/bin/env python3
"""Compile portable firmware logic and verify constant-folded C vectors.

Espressif's clang distribution has no native host backend, but it can emit
optimized LLVM IR.  Each harness function calls the real extracted C code with
a deterministic vector; successful constant folding leaves the exact result in
the function's return instruction.  This catches both syntax/target issues and
behavior drift without maintaining a second implementation of the DSP math.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from chord_engine_check import main as check_chords
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def find_clang() -> Path:
    configured = os.environ.get("ESP_CLANG")
    if configured:
        candidate = Path(configured)
        if candidate.is_file():
            return candidate

    tools_root = Path(os.environ.get("IDF_TOOLS_PATH", Path.home() / ".espressif"))
    candidates = sorted(tools_root.glob("tools/esp-clang/*/esp-clang/bin/clang.exe"), reverse=True)
    if candidates:
        return candidates[0]
    raise RuntimeError("Espressif clang not found; set ESP_CLANG to clang.exe")


COMPILE_ONLY_SUITES = ("audio_meter", "pitch_detector")


SUITES = {
    "looper_view": {
        "host_vector_looper_view_recording": 1,
        "host_vector_looper_view_sync_wait": 1,
        "host_vector_looper_view_count_in": 1,
        "host_vector_looper_view_zero_rate": 1,
    },
    "settings": {
        "host_vector_settings_adsr_preset": 1,
        "host_vector_settings_glide_level": 1,
        "host_vector_settings_delay_level": 1,
        "host_vector_settings_tremolo_level": 1,
        "host_vector_settings_voice_level": 1,
    },
    "playback": {
        "host_vector_playback_strum_release": 1,
        "host_vector_playback_strum_error": 1,
        "host_vector_playback_repeat_wrap": 1,
        "host_vector_playback_arp_delayed_poll": 1,
    },
    "mic_finalize": {
        "host_vector_mic_finalize_pitched": 1,
        "host_vector_mic_finalize_raw": 1,
        "host_vector_mic_finalize_analysis_error": 1,
        "host_vector_mic_finalize_install_error": 1,
    },
    "menu": {
        "host_vector_menu_direction_edges": 1,
        "host_vector_menu_sound_and_bpm": 1,
        "host_vector_menu_priority": 1,
        "host_vector_sound_browse_wrap": 1,
    },
    "amy_prng": {
        "host_vector_amy_prng_repeatable": 1,
        "host_vector_amy_prng_streams_are_distinct": 1,
        "host_vector_amy_prng_order_independent": 1,
    },
    "calibration_store": {
        "host_vector_store_load_valid": 42,
        "host_vector_store_namespace_missing": 0x105,
        "host_vector_store_key_missing": 0x105,
        "host_vector_store_wrong_size": 0x104,
        "host_vector_store_invalid_blob": 0x103,
        "host_vector_store_erase_missing": 1,
    },
    "amy_event_builder": {
        "host_vector_amy_raw_dispatch": 1,
        "host_vector_amy_patch_dispatch": 1,
        "host_vector_amy_mono_dispatch": 1,
        "host_vector_amy_raw_velocity": 1000,
        "host_vector_amy_raw_note": 6025,
        "host_vector_amy_raw_modulation": 11,
        "host_vector_amy_raw_filter": 500,
        "host_vector_amy_raw_envelope": 10700,
        "host_vector_amy_raw_pcm": 421,
        "host_vector_amy_raw_pan": 1000,
        "host_vector_amy_patch_default_velocity": 550,
        "host_vector_amy_patch_portamento": 140,
    },
    "calibration_math": {
        "host_vector_centered_deadzone": 0,
        "host_vector_centered_positive": 500,
        "host_vector_centered_negative": -500,
        "host_vector_centered_inverted": -500,
        "host_vector_range_quarter": 250,
        "host_vector_range_inverted": 750,
        "host_vector_range_clamped": 1000,
        "host_vector_range_invalid": 0,
    },
    "looper_dsp": {
        "host_vector_encode_positive": 32767,
        "host_vector_encode_negative": -32768,
        "host_vector_gain_q8": 750,
        "host_vector_percent_gain": 850,
        "host_vector_bus_gain_one": 256,
        "host_vector_bus_gain_six": 110,
        "host_vector_bus_gain_clamped": 110,
        "host_vector_peak_half": 50,
        "host_vector_limit_threshold": 24576,
        "host_vector_limit_positive": 32767,
        "host_vector_limit_negative": -32768,
        "host_vector_clip_counter": 2,
    },
    "mic_sample_dsp": {
        "host_vector_mic_right_slot": 1122,
        "host_vector_mic_channel_isolation": 60,
        "host_vector_mic_capacity_clamp": 17,
        "host_vector_mic_fixed_gain": 500,
        "host_vector_mic_saturation_meter": 2,
    },
    "looper_track": {
        "host_vector_looper_raw_capture": 1,
        "host_vector_looper_exact_wrap": 12341234,
        "host_vector_looper_muted_advances": 3,
        "host_vector_looper_target_completion": 4,
    },
    "transport_regression": {
        "host_vector_transport_bar_matches_step_grid": 1,
        "host_vector_transport_frame_span": 83338,
        "host_vector_transport_long_run": 18897637,
        "host_vector_sound_menu_owns_joystick": 2,
        "host_vector_drum_mode_routes_to_drums": 6,
        "host_vector_capture_stops_generator": 1,
        "host_vector_manual_capture_keeps_generator_state": 0,
        "host_vector_cancelled_capture_keeps_generator": 0,
        "host_vector_late_generator_capture": 1,
    },
    "looper_transport": {
        "host_vector_looper_command_fifo": 12,
        "host_vector_looper_command_full": 0x101,
        "host_vector_looper_command_wrap": 15,
        "host_vector_looper_command_high_water": 3,
        "host_vector_looper_command_rejected": 104,
        "host_vector_looper_snapshot_consistent": 24,
        "host_vector_looper_snapshot_pinned": 1,
        "host_vector_looper_snapshot_skipped": 17,
    },
    "rhythm": {
        "host_vector_rhythm_delayed_poll": 5004,
        "host_vector_rhythm_swing_phase": 5002,
        "host_vector_rhythm_tick_wrap": 449,
        "host_vector_rhythm_long_run": 600000,
    },
    "control_logic": {
        "host_vector_debounce_bounce_and_hold": 1,
        "host_vector_debounce_release_wrap": 1,
        "host_vector_direction_center": 0,
        "host_vector_direction_up": 1,
        "host_vector_direction_down_left": 6,
        "host_vector_direction_right_boundary": 3,
        "host_vector_queue_overwritten": 2,
        "host_vector_queue_first_retained": 2,
        "host_vector_queue_empty_result": 0x105,
    },
    "wheel": {
        "host_vector_wheel_initial_relative_no_delta": 0,
        "host_vector_wheel_relative_accumulates": 4,
        "host_vector_wheel_relative_decreases": -3,
        "host_vector_wheel_target_switch_without_motion": 0,
        "host_vector_wheel_fixed_step": 46,
        "host_vector_wheel_lower_clamp": 0,
        "host_vector_wheel_upper_clamp": 100,
        "host_vector_wheel_absolute_target_parity": 1011,
        "host_vector_wheel_attack_round_trip": 38,
        "host_vector_wheel_release_round_trip": 73,
        "host_vector_encoder_partial_detent": 0,
        "host_vector_encoder_complete_detent": 1,
        "host_vector_encoder_reverse_partial": 1,
        "host_vector_encoder_complete_reverse": 0,
    },
    "pcf857x": {
        "host_vector_pcf8574_init_latch": 0xFF,
        "host_vector_pcf8575_init_latch": 0xFFFF,
        "host_vector_pcf8575_write_encoding": 0x2ABCD,
        "host_vector_pcf8575_read_decoding": 0x1234,
        "host_vector_pcf8575_failed_write_latch": 0xFFFF,
        "host_vector_pcf8574_pin_bound": 0x102,
    },
}


def parse_constant_return(ir: str, function: str) -> int:
    body_match = re.search(
        rf"define\s+[^@]+@{re.escape(function)}\([^)]*\)[^{{]*\{{(.*?)^\}}",
        ir,
        re.MULTILINE | re.DOTALL,
    )
    if body_match is None:
        raise RuntimeError(f"optimized IR lost vector function {function}")
    returns = re.findall(r"ret\s+i\d+\s+(-?\d+)", body_match.group(1))
    if len(returns) != 1:
        raise RuntimeError(f"{function} did not fold to one constant return")
    return int(returns[0])


def main() -> None:
    check_chords()
    clang = find_clang()
    with tempfile.TemporaryDirectory(prefix="esp-amy-host-") as temporary:
        for suite in (*COMPILE_ONLY_SUITES, *SUITES):
            vectors = SUITES.get(suite, {})
            llvm_ir = Path(temporary) / f"{suite}.ll"
            subprocess.run(
                [
                    str(clang),
                    "--target=riscv32-esp-unknown-elf",
                    "-O3",
                    # State-machine traces need deeper inlining to fold.
                    *(["-mllvm", "-inline-threshold=10000"] if suite in ("playback", "looper_view") else []),
                    "-ffreestanding",
                    "-S",
                    "-emit-llvm",
                    "-Itest/host",
                    "-Imain",
                    "-Icomponents/pcf857x",
                    "-Icomponents/pcf8574",
                    "-Icomponents/pcf8575",
                    "-Icomponents/amy/amy/src",
                    f"test/host/{suite}_harness.c",
                    "-o",
                    str(llvm_ir),
                ],
                cwd=ROOT,
                check=True,
            )
            ir = llvm_ir.read_text(encoding="utf-8")
            for function, expected in vectors.items():
                actual = parse_constant_return(ir, function)
                if actual != expected:
                    raise RuntimeError(f"{function}: expected {expected}, got {actual}")
    print("PASS: portable firmware C parity vectors hold")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
