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


SUITES = {
    "calibration_store": {
        "host_vector_store_load_valid": 42,
        "host_vector_store_namespace_missing": 0x105,
        "host_vector_store_key_missing": 0x105,
        "host_vector_store_wrong_size": 0x104,
        "host_vector_store_invalid_blob": 0x103,
        "host_vector_store_erase_missing": 1,
    },
    "amy_event_builder": {
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
        "host_vector_bus_gain_six": 110,
        "host_vector_bus_gain_clamped": 110,
        "host_vector_peak_half": 50,
        "host_vector_limit_threshold": 24576,
        "host_vector_limit_positive": 28671,
        "host_vector_limit_negative": -29925,
        "host_vector_limited_counter": 2,
        "host_vector_clip_counter": 1,
    },
    "control_logic": {
        "host_vector_direction_center": 0,
        "host_vector_direction_up": 1,
        "host_vector_direction_down_left": 6,
        "host_vector_direction_right_boundary": 3,
        "host_vector_queue_overwritten": 2,
        "host_vector_queue_first_retained": 2,
        "host_vector_queue_empty_result": 0x105,
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
    with tempfile.TemporaryDirectory(prefix="esp-amy-host-") as temporary:
        for suite, vectors in SUITES.items():
            llvm_ir = Path(temporary) / f"{suite}.ll"
            subprocess.run(
                [
                    str(find_clang()),
                    "--target=riscv32-unknown-elf",
                    "-O3",
                    "-ffreestanding",
                    "-S",
                    "-emit-llvm",
                    "-Itest/host",
                    "-Imain",
                    "-Icomponents/pcf857x",
                    "-Icomponents/pcf8574",
                    "-Icomponents/pcf8575",
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
