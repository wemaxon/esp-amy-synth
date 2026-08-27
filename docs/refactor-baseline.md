# Refactor baseline

This document records behavior that modernization work must preserve.  It is a
compatibility contract for refactors, not a redesign proposal.

## Ownership boundaries

- `main/` owns firmware startup, board bring-up, diagnostics, HMI, application
  policy, the AMY adapter, and the looper.
- The small components under `components/` are repository-owned hardware
  adapters.  Their public headers are compatibility surfaces.
- `components/amy/amy` is a pinned upstream dependency. Routine refactors must
  not edit it or clean its working tree. Dependency upgrades may carry narrowly
  scoped platform fixes when upstream still lacks them; AMY integration changes
  otherwise belong in `main/synth/amy_*` or the wrapping component CMake file.
- Board selection remains compile-time configuration.  ESP32 defaults to
  `ai_thinker_a1s`; ESP32-P4 defaults to `wt0132p4_a1`.

## Runtime topology and timing

- `app_main` starts the top-level application task on core 0 with priority 5.
- `synth_app_start` owns the synth application singleton and its 20 ms update
  loop.  Its public signature remains stable.
- Sound browsing applies the selected sound after 180 ms unless the current
  play state requires an immediate change.
- Control switch and axis acquisition has priority over cooperative OLED page
  writes.  Expander interrupt handling retains a slow-poll fallback.
- AMY rendering and looper processing are real-time paths.  Refactors must not
  add allocation, logging, NVS, I2C, or blocking locks to audio processing.

## Persistent data

The following values are persisted and their namespace, key, magic, version,
and binary layout are compatibility-sensitive:

| Data | Namespace | Key | Version |
| --- | --- | --- | --- |
| Joystick calibration | `joy_cal` | `data` | 1 |
| Wheel calibration | `wheel_cal` | `data` | 1 |

Invalid or incompatible data is ignored and board defaults are used.  Reset
returns the in-memory value to board defaults even when the key is absent.

## Stable public interfaces

The public facades in `synth/app.h`, `synth/amy_engine.h`, `synth/looper.h`,
`hmi/controls.h`, the calibration headers, codec header, and local component
headers stay source-compatible during behavior-preserving passes.  A symbol
with no in-repository caller is not dead if it is declared by one of these
headers.

## Parity gates

Every refactor pass must keep these checks green:

1. `python tools/chord_engine_spec_check.py`
2. `python tools/firmware_refactor_check.py`
3. `python tools/host_parity_check.py`
4. An isolated ESP32 build using `sdkconfig.defaults.esp32`
5. An isolated ESP32-P4 build using `sdkconfig.defaults.esp32p4`

Behavior-sensitive milestones additionally require device smoke checks for
boot modes, controls, OLED responsiveness, AMY playback/effects, calibration
persistence, and looper record/playback/synchronization.

## Implemented refactor passes

| Pass | Current behavior preserved | Structural improvement | Validation check |
| --- | --- | --- | --- |
| Remove stale test path | Firmware boots through the real synth entrypoint. | Replaced the hello-world template pytest with a firmware boot-log smoke test. | Structural check requires the real startup log; device pytest remains the hardware gate. |
| Slim the app coordinator | The 20 ms loop and UI contents remain unchanged. | Moved private aggregate state and UI-model projection into `synth/app/`. | Timing/public-contract checks plus both target builds. |
| Simplify AMY integration | Voice allocation, event fields, effects, and note-off behavior remain unchanged. | Removed pass-through runtime wrappers and extracted AMY event construction. | Chord invariants, AMY C vectors, and both target builds. |
| Isolate looper DSP | Sample encoding, gains, metering, limiting, and counters retain the same arithmetic. | Moved pure sample operations out of the synchronized looper state module. | Exact optimized-C DSP vectors and both target builds. |
| Isolate control logic | Joystick boundaries and the 15-event overwrite-oldest queue behavior remain unchanged. | Moved direction selection and ring-buffer operations out of the hardware service. | Boundary and overflow C vectors. |
| Consolidate calibration | NVS schemas, defaults, normalization, capture, save, and reset remain compatible. | Shared NVS lifecycle and pure normalization helpers replace duplicated paths. | Schema checks, math vectors, and both target builds. |
| Consolidate PCF transport | PCF8574/PCF8575 facades, byte order, latches, and error propagation remain stable. | Shared I2C transport removes duplicate device/read/write plumbing. | Fake-I2C C vectors and both target builds. |
| Repair target configuration | ESP32 starts its ES8388 at 100%; ESP32-P4 starts its ES8390 at 100%. | Board profiles own both defaults and disabled private helpers are compile-guarded. | Board-contract check and isolated clean builds for both targets. |
| Isolate looper real-time ownership | Six-track record/play, metronome, controls, DSP, and UI fields remain stable; commands apply at the next audio block. | The audio task solely owns one authoritative state per track, fed by a bounded SPSC queue and publishing immutable triple-buffered snapshots. | Transport/track C vectors, structural no-lock checks, both target builds, and the pending WT0132P4 profile matrix. |
| Stabilize drum capture and controls | Sound browsing leaves running drums intact; FREE recording stays manual and fixed bars retain their selected musical length. | Reserved AMY drum oscillators, explicit joystick ownership, shared rational timing, raw mono capture, and a successful-capture handoff remove cross-feature interference. | AMY structural gates, route/timing/capture/wrap vectors, target build, and device rhythm/timbre parity checks. |

## Separate migration tasks

The following are intentionally not part of behavior-preserving refactoring:

- ESP-IDF, compiler, component, and AMY submodule upgrades need their own
  compatibility matrix, release-note review, clean builds, and hardware soak.
- Public facade changes and calibration/NVS layout changes need versioned API
  and data migrations rather than opportunistic signature or struct edits.
- Broader audio/control task topology changes, multicore rendering, and PSRAM
  pool redesign remain separate architecture migrations with their own timing
  and underrun/stress baselines.
- Implementing currently visible but unsupported play modes, or changing the
  control queue overflow policy, is a functional feature task with UI/manual
  parity tests.
