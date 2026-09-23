# On-device instrument level calibration

The production firmware uses one reviewed, compile-time gain table for melodic
sounds and one for each drum `(kit, role)` pair. Note velocity remains a
performance control; calibration never rewrites it. There is no runtime AGC,
compressor, NVS calibration state, hidden limiter, or fallback table.

The checked-in table initially has revision `uncalibrated-unity`. Those unity
values intentionally preserve existing sound. Replace them only with a complete
report captured from the target WT0132P4 device.

## Architecture and real-time contract

The diagnostic meter has two render taps:

- `dry`: directly after `amy_update()`, before flanger and looper processing;
- `output`: after all DSP and immediately before the blocking I2S write.

The audio task exclusively owns K-weighting filter state, peak/RMS accumulators,
the 400 ms momentary window, and exposure accumulation. The app task sends one
bounded SPSC request and reads triple-buffered immutable snapshots. The audio
callback takes no mutex, allocates no memory, and emits no log output. Each
record includes the maximum meter execution time and deadline-miss count.
K-weighting coefficients are derived once at meter initialization from AMY's
actual stream sample rate; coefficient calculation never runs on the audio path.
The request also owns optional fixture start/stop hooks. The audio task invokes
them immediately before rendering a block, so metering and note onset share one
deterministic boundary and melodic note-off occurs after an exact whole-block
hold interval.

Normal builds leave `ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED=0`. The render
tap calls and diagnostic runner are compiled out, and their unreferenced meter
object is not linked into the application.

## Fixture specification

The runner disables filter, reverb, chorus, delay, tremolo, flanger, LFO, and
stereo pairs for canonical measurements. It measures every built-in melodic
sound except the user and microphone PCM slots at MIDI notes 48, 60, and 72,
then a C chord. A six-note output-path stress fixture uses the firmware's
configured stereo capability. Every drum kit/role pair receives one velocity
100 hit. The complete set runs twice to expose nondeterminism.

Every fixture begins with an exclusive AMY graph reset that discards queued
events and rebuilds oscillator, patch allocator, bus DSP, and wrapper state.
This prevents release tails or previously allocated patch voices from entering
the next measurement; both repetitions therefore begin each fixture from the
same canonical engine state.

Noise and Karplus-Strong oscillators use independent deterministic PRNG streams
derived from their oscillator number. No random state is shared between AMY's
two render cores, so polyphonic noise patches are race-free and reproduce the
same waveform after an engine reset without disabling multicore rendering.

The measurement run sets AMY's master volume to 100% so the 3 dBFS reserve is
valid at the maximum supported master setting. It restores the board default
(80% on WT0132P4) before normal playback resumes.

Metrics are sample peak, clipped-sample count, RMS, BS.1770 K-weighted fixed
window loudness, maximum 400 ms momentary loudness, and K-weighted sound
exposure. The physical speaker amp is shut down for the run. The OLED only
shows progress from the app task. JSONL is written to the serial console.

The automatic sweep contains 304 fixtures and takes about 22 minutes.

## Build and capture

Build a dedicated P4 image without changing a board header:

```powershell
idf.py -B build-level-cal `
  -D ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED=1 `
  -D ESP_POCKET_AUDIO_AUTO_LEVEL_CALIBRATION_MODE=1 build
idf.py -B build-level-cal -p COMx flash monitor
```

Capture the complete console stream to `level-report.jsonl`. Non-JSON ESP-IDF
log lines are allowed; the generator ignores them. UTF-8 and BOM-marked UTF-16
PowerShell captures are accepted. If automatic mode is off, holding SOUND and
MODE together during boot starts the same exclusive run.

Do not generate gains unless the report ends with a successful `run_end` whose
`completed` and `expected` counts match.

## Generate and review

```powershell
python tools/generate_level_calibration.py level-report.jsonl `
  --output main/synth/level_calibration.generated.h `
  --summary level-calibration-summary.json
```

The generator rejects clipped fixtures, audio-meter deadline misses, incomplete
fixture coverage, fewer than two repetitions, and repeat spread over 0.25 LU.
It removes the gain already active during measurement, anchors melodic balance
to the current catalog median, and lowers the shared target when required to
retain at least 3 dBFS stress headroom.

Drum event exposure is targeted relative to the melodic anchor by role:

| Role | Target offset |
| --- | ---: |
| Kick | 0 dB |
| Alternate kick | -1 dB |
| Snare/clap | -1 dB |
| Tom | -3 dB |
| Bell | -6 dB |
| Open hat | -6 dB |
| Closed hat | -8 dB |

If any projected drum peak violates the 3 dBFS reserve, all role targets move
down together so their balance remains intact. The generator does not clamp an
individual gain or substitute a fallback value.

Review the generated header and summary together. Commit the source report or
an immutable artifact reference, the summary, and the generated header in one
calibration-only change. A new AMY version, sample set, instrument definition,
sample rate, channel topology, or fixture definition invalidates the revision
and requires a fresh hardware sweep.

## Parity gates

Before accepting a generated table:

```powershell
python tools/level_calibration_check.py
python tools/host_parity_check.py
python tools/firmware_refactor_check.py
idf.py build
```

Also build the calibration configuration, rerun the full device sweep with the
new gains active, and verify zero clips, zero meter deadline misses, repeat
stability, and no output-path peak above -3 dBFS. Listening checks remain a
separate acceptance step; they do not replace the objective parity report.
