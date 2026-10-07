# Firmware testability plan

## Decisions

- Linux is the canonical host-test and CI environment; use Ubuntu under WSL2
  locally. The simulator will be headless, producing WAVs and state traces.
- ESP32-P4 / WT0132P4-A1 is the active hardware target. A board can be connected
  for automated tests. Ai-Thinker A1S is deprecated and no longer available;
  removing its firmware support is a separate change, not part of test migration.
- Keep ESP-IDF and AMY pinned. No dependency upgrade or framework migration is
  required by this plan. Preserve local AMY modifications.
- No automatic hardware-to-simulation fallback. Each target selects its explicit
  implementation. A missing compiler, dependency, or required device is an error.

## Architecture

Portable application logic owns explicit musical/application state. Time and
inputs enter through small boundaries. Audio processing runs the production AMY,
effects, looper and capture code on buffers. Platform integration owns FreeRTOS,
I2S/DMA, drivers, storage and physical UI. Share implementation, not a second
Python model of firmware behavior. Avoid generic event buses or driver frameworks.

Native tests establish logic/DSP behavior; a headless simulator establishes
scripted application/audio integration; target builds establish configuration and
link compatibility; hardware establishes electrical behavior and real deadlines.
QEMU is optional for a concrete supported ESP32 integration test, not the audio
test foundation. The published Espressif matrix does not provide P4, I2C or I2S
coverage needed for this board:
https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md

## Reviewable implementation steps

1. **Standardize native builds and remove harness workarounds.** Preserve all
   existing vectors and expected results. Use CMake/CTest, separately compiled
   production translation units, real C library math, and production atomics.
   Remove LLVM constant-return inspection, direct `.c` inclusion, header-guard
   suppression and duplicate production types. Keep explicit, narrowly scoped
   platform test doubles where hardware is intentionally outside a unit test.
   Validate Debug and Release execution, existing repository checks and the P4
   firmware build. Publish CTest results in CI.
2. **Establish application/audio boundaries.** Move time, storage and hardware
   calls out of portable decisions without changing event or block order.
   Characterize each extracted boundary before moving code; compare traces.
3. **Build the headless simulator.** Replay timestamped controls and microphone
   WAVs with virtual time through production application/AMY code. Export audio,
   UI state and event traces. Begin with chord, sound-change and looper scenarios.
4. **Add quality gates.** Sanitizers, coverage, boundary/property tests, deliberate
   fault injection and a small reviewed audio regression corpus. Exact comparisons
   for discrete/integer behavior; documented tolerances for floating-point audio.
   Never regenerate reference outputs merely to make a failure disappear.
5. **Automate P4 hardware and performance.** Explicit device identity/reservation,
   flash/run/capture timeouts, calibration preservation, the test console and
   `tools/hil.py` (see maintenance.md), optional audio loopback. Separate render cost, I/O wait, scheduling latency and underruns.
   Export bounded counters outside the audio callback. Test simultaneous controls,
   display, microphone, effects and six-track looping under load.
6. **Evaluate QEMU only for uncovered supported behavior.** Do not maintain custom
   peripheral models just to emulate this instrument.

## Tooling and repository direction

Use `tests/` for tests, fixtures and explicit test doubles; `tools/` for thin
orchestration/analysis; ignored `build/` for isolated configurations and reports.
Keep production modules where they are in step 1: mechanical movement into
cohesive core/audio/platform components belongs to step 2. Keep upstream AMY in
its existing submodule. No archive of completed refactor reports.

Commands must be noninteractive, bounded, and return failure status. Artifacts
must identify scenario, seed, toolchain, configuration and revision. Distinguish
passed, failed and unavailable hardware. Neither native nor QEMU benchmarks
establish target real-time performance; verify deadlines on the P4.

At 256 frames / 44.1 kHz a block spans about 5.80 ms. Hardware acceptance also
accounts for DMA buffering. Measure distributions/maxima, deadline misses,
underruns, queue overflow, stack headroom, heap/PSRAM and input-to-audio latency.
Without hardware runs, report these checks as unverified, not passed.

## Current status

Step 1 native migration is implemented: 21 CTest suites execute in Debug and
Release on Ubuntu 24.04/GCC 13.3. Native presets live in `tests/native` so they
cannot be implicitly selected by ESP-IDF. Existing Python check entry points
remain available. SDK boundary doubles remain explicit; production types, libc
and atomics are no longer substituted. Audio-meter and pitch-detector vectors
now execute. Looper capacity vectors use the production 32-entry queue, and the
sampled-square pitch vector checks a documented numerical tolerance.

The ESP32-P4 build passes with ESP-IDF 6.0.1. Its binary is 833,280 bytes and
static DIRAM usage is 126,238 bytes. No pre-change size comparison or hardware
timing/parity is claimed. No board was flashed.

The existing AMY firmware changes and PRNG header are captured in
`components/amy/patches`, with an explicit pinned-revision application command.
Native CI prepares a fresh dependency and runs Debug/Release suites, repository
contracts and JUnit artifact upload. Python-only AMY modifications remain local.
The original submodule working tree is preserved. Hosted CI execution must still
be verified after publishing these repository changes.
