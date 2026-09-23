#!/usr/bin/env python3
"""Generate reviewed static instrument gains from device JSONL measurements."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOUND_ENUMS = (
    "SYNTH_SOUND_SAW",
    "SYNTH_SOUND_SINE",
    "SYNTH_SOUND_TRIANGLE",
    "SYNTH_SOUND_SQUARE",
    "SYNTH_SOUND_FM_EPIANO",
    "SYNTH_SOUND_FM_BELL",
    "SYNTH_SOUND_FM_HX7",
    "SYNTH_SOUND_STRINGS",
    "SYNTH_SOUND_FLUTE",
    "SYNTH_SOUND_BRASS",
    "SYNTH_SOUND_CLARINET",
    "SYNTH_SOUND_WURLI",
    "SYNTH_SOUND_VOX_AHH",
    "SYNTH_SOUND_VIBRAPHONE",
    "SYNTH_SOUND_ACOUSTIC",
    "SYNTH_SOUND_HARP",
    "SYNTH_SOUND_HUMMING",
    "SYNTH_SOUND_ROBBO",
    "SYNTH_SOUND_SHUTTER",
    "SYNTH_SOUND_JUNO_POLY",
    "SYNTH_SOUND_OCEAN_PAD",
    "SYNTH_SOUND_SAWSQUARE",
    "SYNTH_SOUND_MIC_SAMPLE",
    "SYNTH_SOUND_USER_SAMPLE",
)

DRUM_KIT_ENUMS = (
    "SYNTH_DRUM_KIT_TIGHT",
    "SYNTH_DRUM_KIT_X0X",
    "SYNTH_DRUM_KIT_X9X",
    "SYNTH_DRUM_KIT_LYNN",
    "SYNTH_DRUM_KIT_KR78",
    "SYNTH_DRUM_KIT_TRAP",
)

DRUM_ROLE_TARGET_DB = (0.0, -1.0, -1.0, -8.0, -3.0, -6.0, -6.0)
CANONICAL_MELODIC_FIXTURES = frozenset(("note_48", "note_60", "note_72", "chord_c"))
STRESS_FIXTURE = "poly_stress"


class CalibrationError(RuntimeError):
    pass


@dataclass(frozen=True)
class GainPlan:
    sound_gain: tuple[float, ...]
    drum_gain: tuple[tuple[float, ...], ...]
    melodic_target_lufs: float
    drum_headroom_shift_db: float
    revision: str


def gain_db(gain: float) -> float:
    if not math.isfinite(gain) or gain <= 0.0:
        raise CalibrationError(f"invalid applied gain {gain!r}")
    return 20.0 * math.log10(gain)


def load_report(path: Path) -> tuple[list[dict], bytes]:
    payload = path.read_bytes()
    if payload.startswith((b"\xff\xfe", b"\xfe\xff")):
        decoded = payload.decode("utf-16")
    else:
        decoded = payload.decode("utf-8-sig")
    records: list[dict] = []
    for line_number, raw_line in enumerate(decoded.splitlines(), 1):
        line = raw_line.strip()
        if not line.startswith("{"):
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise CalibrationError(f"{path}:{line_number}: malformed JSON: {exc}") from exc
        if record.get("schema") == "esp-pocket-audio.level.v1":
            records.append(record)
    if not records:
        raise CalibrationError("no level-calibration JSON records found")
    beginnings = [record for record in records if record.get("type") == "run_begin"]
    if len(beginnings) != 1:
        raise CalibrationError("report must contain exactly one run_begin record")
    beginning = beginnings[0]
    if (beginning.get("sample_rate_hz") != 44100 or beginning.get("channels") != 2 or
            beginning.get("repeats") != 2 or beginning.get("master_volume") != 1.0):
        raise CalibrationError(f"report fixture configuration is not canonical: {beginning}")
    endings = [record for record in records if record.get("type") == "run_end"]
    if len(endings) != 1:
        raise CalibrationError("report must contain exactly one run_end record")
    ending = endings[0]
    if ending.get("result") != 0 or ending.get("completed") != ending.get("expected"):
        raise CalibrationError(f"device fixture run was incomplete: {ending}")
    return [record for record in records if record.get("type") == "measurement"], payload


def validate_repetitions(records: Iterable[dict], max_variance_lu: float) -> None:
    grouped: dict[tuple, list[tuple[int, float]]] = defaultdict(list)
    for record in records:
        key = (
            record.get("category"),
            record.get("fixture"),
            record.get("sound"),
            record.get("note"),
            record.get("kit"),
            record.get("voice"),
        )
        value = float(record["lufs"] if record["category"] == "melodic"
                      else record["exposure_lufs"])
        if not math.isfinite(value):
            raise CalibrationError(f"non-finite loudness measurement: {key}")
        grouped[key].append((int(record["repeat"]), value))
        if int(record.get("clipped_samples", 0)) != 0:
            raise CalibrationError(f"clipped fixture cannot be calibrated: {key}")
        if int(record.get("meter_deadline_misses", 0)) != 0:
            raise CalibrationError(f"meter missed its audio deadline: {key}")

    incomplete = [
        key for key, values in grouped.items()
        if sorted(repeat for repeat, _ in values) != [0, 1]
    ]
    if incomplete:
        raise CalibrationError(
            f"fixtures require exactly repetitions 0 and 1; first invalid: {incomplete[0]}"
        )
    unstable = [
        (key, max(value for _, value in values) - min(value for _, value in values))
        for key, values in grouped.items()
        if max(value for _, value in values) - min(value for _, value in values) > max_variance_lu
    ]
    if unstable:
        key, variance = max(unstable, key=lambda item: item[1])
        raise CalibrationError(
            f"fixture variance {variance:.3f} LU exceeds {max_variance_lu:.3f} LU: {key}"
        )


def corrected_db(record: dict, field: str) -> float:
    return float(record[field]) - gain_db(float(record["applied_gain"]))


def mean(values: Iterable[float]) -> float:
    collected = list(values)
    if not collected:
        raise CalibrationError("required fixture group is empty")
    return statistics.fmean(collected)


def c_float(value: float) -> str:
    rendered = f"{value:.9g}"
    if "." not in rendered and "e" not in rendered:
        rendered += ".0"
    return rendered + "f"


def derive_plan(records: list[dict], report_payload: bytes, peak_limit_dbfs: float) -> GainPlan:
    melodic = [record for record in records if record.get("category") == "melodic"]
    drums = [record for record in records if record.get("category") == "drum"]

    sound_baseline: list[float] = []
    sound_stress_peak: list[float] = []
    for sound in range(22):
        canonical = [
            record for record in melodic
            if record.get("sound") == sound and
            record.get("fixture") in CANONICAL_MELODIC_FIXTURES
        ]
        stress = [
            record for record in melodic
            if record.get("sound") == sound and record.get("fixture") == STRESS_FIXTURE
        ]
        if not canonical or not stress:
            raise CalibrationError(f"missing melodic fixtures for sound {sound}")
        sound_baseline.append(statistics.median(corrected_db(record, "lufs")
                                                for record in canonical))
        sound_stress_peak.append(max(corrected_db(record, "peak_dbfs") for record in stress))

    anchor = statistics.median(sound_baseline)
    headroom_ceiling = min(
        baseline + peak_limit_dbfs - peak
        for baseline, peak in zip(sound_baseline, sound_stress_peak)
    )
    melodic_target = min(anchor, headroom_ceiling)
    sound_gain = [10.0 ** ((melodic_target - baseline) / 20.0)
                  for baseline in sound_baseline]
    sound_gain.extend((1.0, 1.0))  # user-provided PCM has no canonical source material

    drum_baseline: list[list[float]] = []
    drum_peak: list[list[float]] = []
    for kit in range(len(DRUM_KIT_ENUMS)):
        kit_baseline: list[float] = []
        kit_peak: list[float] = []
        for voice in range(len(DRUM_ROLE_TARGET_DB)):
            fixtures = [
                record for record in drums
                if record.get("kit") == kit and record.get("voice") == voice
            ]
            if not fixtures:
                raise CalibrationError(f"missing drum fixture for kit {kit}, voice {voice}")
            kit_baseline.append(mean(corrected_db(record, "exposure_lufs")
                                     for record in fixtures))
            kit_peak.append(max(corrected_db(record, "peak_dbfs") for record in fixtures))
        drum_baseline.append(kit_baseline)
        drum_peak.append(kit_peak)

    desired_drum_target = [melodic_target + offset for offset in DRUM_ROLE_TARGET_DB]
    worst_overage = max(
        drum_peak[kit][voice] + desired_drum_target[voice] - drum_baseline[kit][voice] -
        peak_limit_dbfs
        for kit in range(len(DRUM_KIT_ENUMS))
        for voice in range(len(DRUM_ROLE_TARGET_DB))
    )
    drum_shift = max(0.0, worst_overage)
    drum_gain = tuple(
        tuple(
            10.0 ** ((desired_drum_target[voice] - drum_shift - drum_baseline[kit][voice]) /
                     20.0)
            for voice in range(len(DRUM_ROLE_TARGET_DB))
        )
        for kit in range(len(DRUM_KIT_ENUMS))
    )

    revision_input = report_payload + f"|peak={peak_limit_dbfs:.6f}|v1".encode("ascii")
    revision = hashlib.sha256(revision_input).hexdigest()[:16]
    return GainPlan(tuple(sound_gain), drum_gain, melodic_target, drum_shift, revision)


def render_header(plan: GainPlan) -> str:
    lines = [
        "/* Generated by tools/generate_level_calibration.py.  Do not hand tune. */",
        "#ifndef SYNTH_LEVEL_CALIBRATION_GENERATED_H",
        "#define SYNTH_LEVEL_CALIBRATION_GENERATED_H",
        "",
        f'#define SYNTH_LEVEL_CALIBRATION_REVISION "{plan.revision}"',
        "",
        "static const float SYNTH_LEVEL_SOUND_GAIN[SYNTH_SOUND_COUNT] = {",
    ]
    lines.extend(
        f"    [{enum}] = {c_float(gain)}," for enum, gain in zip(SOUND_ENUMS, plan.sound_gain)
    )
    lines.extend(("};", "", "static const float SYNTH_LEVEL_DRUM_GAIN"
                  "[SYNTH_DRUM_KIT_COUNT][SYNTH_DRUM_VOICE_COUNT] = {"))
    for enum, gains in zip(DRUM_KIT_ENUMS, plan.drum_gain):
        formatted = ", ".join(c_float(gain) for gain in gains)
        lines.append(f"    [{enum}] = {{{formatted}}},")
    lines.extend(("};", "", "#endif", ""))
    return "\n".join(lines)


def write_atomic(path: Path, content: str) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="serial JSONL captured from the device")
    parser.add_argument("--output", type=Path, required=True, help="generated firmware header")
    parser.add_argument("--summary", type=Path, help="optional review summary JSON")
    parser.add_argument("--peak-limit-dbfs", type=float, default=-3.0)
    parser.add_argument("--max-variance-lu", type=float, default=0.25)
    args = parser.parse_args()

    records, payload = load_report(args.report)
    validate_repetitions(records, args.max_variance_lu)
    plan = derive_plan(records, payload, args.peak_limit_dbfs)
    write_atomic(args.output, render_header(plan))

    summary = {
        "schema": "esp-pocket-audio.level-plan.v1",
        "revision": plan.revision,
        "melodic_target_lufs": plan.melodic_target_lufs,
        "drum_headroom_shift_db": plan.drum_headroom_shift_db,
        "peak_limit_dbfs": args.peak_limit_dbfs,
        "sound_gain": plan.sound_gain,
        "drum_gain": plan.drum_gain,
    }
    if args.summary:
        write_atomic(args.summary, json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, CalibrationError, ValueError, KeyError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
