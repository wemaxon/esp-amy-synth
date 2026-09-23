#!/usr/bin/env python3
"""Deterministic checks for the level-calibration gain derivation."""

from __future__ import annotations

import json
import math
import tempfile
from pathlib import Path

import generate_level_calibration as calibration


def melodic_record(sound: int, fixture: str, repeat: int) -> dict:
    baseline = -24.0 + sound * 0.2
    return {
        "category": "melodic",
        "fixture": fixture,
        "repeat": repeat,
        "sound": sound,
        "note": -1 if fixture in ("chord_c", "poly_stress") else int(fixture[5:]),
        "kit": -1,
        "voice": -1,
        "applied_gain": 1.0,
        "lufs": baseline,
        "exposure_lufs": baseline,
        "peak_dbfs": -8.0 if fixture == "poly_stress" else -12.0,
        "clipped_samples": 0,
        "meter_deadline_misses": 0,
    }


def drum_record(kit: int, voice: int, repeat: int) -> dict:
    return {
        "category": "drum",
        "fixture": "hit_v100",
        "repeat": repeat,
        "sound": -1,
        "note": -1,
        "kit": kit,
        "voice": voice,
        "applied_gain": 1.0,
        "lufs": -30.0,
        "exposure_lufs": -30.0 + kit * 0.15 + voice * 0.1,
        "peak_dbfs": -10.0,
        "clipped_samples": 0,
        "meter_deadline_misses": 0,
    }


def main() -> None:
    records = []
    fixtures = (*sorted(calibration.CANONICAL_MELODIC_FIXTURES), calibration.STRESS_FIXTURE)
    for repeat in range(2):
        for sound in range(22):
            records.extend(melodic_record(sound, fixture, repeat) for fixture in fixtures)
        for kit in range(len(calibration.DRUM_KIT_ENUMS)):
            records.extend(drum_record(kit, voice, repeat)
                           for voice in range(len(calibration.DRUM_ROLE_TARGET_DB)))

    calibration.validate_repetitions(records, 0.25)

    report_records = [
        {
            "schema": "esp-pocket-audio.level.v1",
            "type": "run_begin",
            "sample_rate_hz": 44100,
            "channels": 2,
            "repeats": 2,
            "master_volume": 1.0,
        },
        *(dict(record, schema="esp-pocket-audio.level.v1", type="measurement")
          for record in records),
        {
            "schema": "esp-pocket-audio.level.v1",
            "type": "run_end",
            "completed": len(records),
            "expected": len(records),
            "result": 0,
        },
    ]
    report_text = "\n".join(json.dumps(record) for record in report_records) + "\n"
    with tempfile.TemporaryDirectory(prefix="esp-amy-level-report-") as temporary:
        for encoding in ("utf-8", "utf-16"):
            report_path = Path(temporary) / f"report-{encoding}.jsonl"
            report_path.write_text(report_text, encoding=encoding)
            loaded, _ = calibration.load_report(report_path)
            if loaded != report_records[1:-1]:
                raise RuntimeError(f"{encoding} report decoding changed measurement records")

    plan = calibration.derive_plan(records, b"deterministic-report", -3.0)
    if len(plan.sound_gain) != len(calibration.SOUND_ENUMS):
        raise RuntimeError("sound gain table does not cover the public catalog")
    if not plan.sound_gain[0] > plan.sound_gain[21]:
        raise RuntimeError("quieter synthetic sounds must receive more gain")
    if any(not math.isfinite(gain) or gain <= 0.0 for gain in plan.sound_gain):
        raise RuntimeError("sound gain must be finite and positive")
    if any(not math.isfinite(gain) or gain <= 0.0
           for kit in plan.drum_gain for gain in kit):
        raise RuntimeError("drum gain must be finite and positive")
    rendered = calibration.render_header(plan)
    if "= 1f," in rendered or "{1f," in rendered:
        raise RuntimeError("generated C float literals require a decimal point")
    print("PASS: level calibration derivation and generated schema hold")


if __name__ == "__main__":
    main()
