# Firmware maintenance

## Build and check

Use the pinned AMY submodule and an ESP-IDF environment (currently validated
with 6.0.1). Do not switch dependency revisions as part of a firmware build.
ESP32-P4 / WT0132P4-A1 is the active hardware target. Ai-Thinker A1S is
deprecated and no longer hardware-tested; its source configuration is retained.
Keep target configurations separate; from the repository root:

```sh
idf.py -B build/esp32p4 -D SDKCONFIG=build/esp32p4/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32p4 -D IDF_TARGET=esp32p4 build
```

Native tests run on Linux (Ubuntu 24.04 / GCC 13 validated), including WSL2.
Install `build-essential`, `cmake` (3.24+), `ninja-build` and `python3`. From
`tests/native`, run both configurations:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-junit ../../build/native-debug/results.xml
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-junit ../../build/native-release/results.xml
```

The existing check commands remain available from the repository root on Linux:

```sh
python tools/chord_engine_spec_check.py
python tools/firmware_refactor_check.py
python tools/host_parity_check.py
python tools/level_calibration_check.py
```

CTest executes 21 suites, compiling production C as separate translation units
with the real standard library and production atomics. Assertions remain active
in Release. Audio-meter and pitch-detector vectors execute, not just compile.
Run chords alone with `python tools/chord_engine_check.py`; the specification
and host-parity commands include them. Python wrappers use the Debug preset.
Build products and JUnit results stay under ignored `build/`.

SDK doubles live only in `tests/native/fakes/esp_idf`; they are unit-test
boundaries, not an ESP-IDF emulator. Playback tests use the real application state.
AMY event tests use upstream event declarations and initialization, intercepting
only enqueue through the linker. Meter tests use a fixed clock for numerical
vectors. These tests do not establish scheduler, concurrency or hardware parity.
The firmware patch, including `src/amy_prng.h`, is captured under
`components/amy/patches`. After initializing a fresh pinned submodule, run
`python3 tools/amy_patches.py` explicitly before building firmware or native tests.
On an existing patched checkout, use `python3 tools/amy_patches.py --check`.
Neither command resets local changes. Native CI applies this same patch and runs
Debug/Release CTest suites with JUnit artifacts. Missing dependencies fail the build.

Device boot checks live in `pytest_firmware.py` and require connected hardware.
Compare build warnings, binary size, RAM use, and relevant device measurements
before and after behavior-sensitive changes.

## Ownership and compatibility

AMY is pinned to upstream tag `1.2.172` (`5e7c4c64`). Firmware uses the supported
`BLOCK_SIZE_BITS=8` setting and AMY's fill-task priority, retaining 256-frame
blocks and the existing task priority. Multicore completion uses upstream's
dedicated semaphore. Keep `platform.multithread=0`: this firmware owns I2S
pacing and calls `amy_update()` from its own audio task.

Local AMY patches still required at this revision are ESP-IDF task signatures,
the reserved-oscillator null check in all-notes-off, oscillator-local random
streams, and EQ allocation cleanup on bus reset. The Windows Python build
adjustments are also retained. Do not remove these based only on similar
upstream fixes in other code paths. A prebuilt local Python extension must be
rebuilt before using it with this revision; firmware does not use that binary.

The chorus requests 512 samples explicitly to preserve the old implementation's
effective sweep; older AMY ignored the configured 320-sample span. Upstream's
filter-state reset and voice-reference bounds fixes are retained. Distortion
and time-stretch APIs are available but are not enabled by existing controls.
Recheck chorus, filter transitions, PCM playback, allocation headroom, and
multicore audio timing on hardware after dependency upgrades.

- `main/` owns startup, board configuration, controls, application policy,
  the AMY adapter, and looper. Local components own hardware protocols.
- `components/amy/amy` is an upstream dependency. Preserve local platform
  patches; upgrades belong in separate changes.
- The application task owns application state and updates every 20 ms.
  Sound browsing applies after 180 ms unless playback requires an immediate change.
- Control acquisition takes priority over cooperative OLED writes. Expander
  interrupts retain a slow-poll fallback. The control queue holds 15 events
  and overwrites the oldest when full.
- Public headers remain source-compatible. No local callers does not make
  an exported function dead. Keep enum values and persistent layouts stable.
- Calibration blobs use `joy_cal/data` and `wheel_cal/data`, version 1.
  Preserve magic, binary layout, validation, board fallback, and reset semantics.
- Keep allocations, logging, NVS, I2C, and blocking locks out of audio callbacks.

### Chords

`chord_engine.c` owns pure chord construction, formula/name metadata, inversions,
and bounded voice-leading search. It has no ESP-IDF, AMY, allocation, or I/O
dependency. `app/chord_tools.c` supplies held-button, lock, and bass context;
the coordinator owns edge-triggered key changes. `amy_voice_map.c` alone reduces
the result to six musical voices. Keep note order by musical role, not pitch:
inversions change pitches without rearranging root/third/fifth slots.

The behavioral reference is the [official 2.9 beta manual](https://hichord.github.io/hichord-beta-updater/manual/)
and [companion source](https://app.hichord.shop/app.js?v=20260701a), inspected
2026-09-21. Default, Extended, Chromatic, Borrow, and OG are exposed. Chromatic
horizontal gestures transpose once per direction edge, including held chords.
Borrow shifts the chord root, not the selected key; locks retain those modifiers.
Existing enum values are retained, with new modes/modifiers appended.

Public-source limits and deliberate local policies (not verified device parity):

- The manual's older Default example says add9, while its 2.9 change notes and
  companion OG comments describe major/minor ninths. Default uses a seventh plus
  ninth; Extended add9 remains distinct. OG replaces that ninth with the doubled
  third. Diminished-family ninths use half-diminished harmony plus a ninth.
- Only the existing major-scale seven-button layout is implemented. HiChord's
  other nine scales remain a gap: exact button voicings, particularly pentatonic
  and blues, are not established by the public material. No guessed scale table.
- Voice leading searches three inversions at the nominal and adjacent octaves,
  minimizing sorted pitch movement with a mean-register penalty. Equal scores
  prefer the requested inversion/register. With no history, manual inversion
  applies. This is a local algorithm; official tie-breaking is unpublished.
- At MIDI boundaries the entire voicing shifts by octaves, not individual notes.
  Invalid requests leave output unchanged. An unrepresentable bass below the
  chord is rejected. Bass normally uses root minus two octaves or the slash degree.
- Six chord tones plus bass omit the fifth-position tone, preserving all three
  extensions. Exact HiChord reduction policy is unknown. Layered multi-button
  chords retain the existing button-order priority and capacity limits.

Tests execute production construction, application chord helpers, and voice
assignment. Independent pitch-class expectations cover all keys, degrees,
modifiers, inversions, and bass modes; explicit vectors cover names, pitches,
Borrow/OG, register limits, locks, slash bass, and voice leading. Passing these
tests proves the specified local behavior, not comparison with HiChord hardware.

### Looper

The audio task owns six tracks, mono `int16_t` sample buffers, transport, and
metronomes. Buffers are preallocated in PSRAM. The application sends commands
through a 32-entry SPSC queue; commands apply at audio-block boundaries. Full
queues reject new commands with `ESP_ERR_NO_MEM` without reordering accepted work.
Three snapshot slots publish coherent views. A writer skips publication if
both alternative slots are pinned; it never waits for readers.

FREE recording stops manually. Fixed 1–8 bar lengths and drum steps use shared
rational timing. Preserve raw captured samples at loop seams. Successful
capture of a running built-in beat stops its generator once; cancelled captures
and manual drum-pad recordings do not. Sound changes release musical voices
without releasing or reallocating the reserved drum oscillators.

## Device acceptance

Hardware timing and parity for the looper ownership, drum capture, instrument
browsing over drums, and coherent input publication still require confirmation.
Host tests alone do not close these checks.

On both boards check boot/diagnostic modes, press/release and simultaneous inputs,
OLED responsiveness, calibration save/reset/reboot, playback/effects, and wheel
behavior. Check [microphone sampling](mic-sampling.md) on WT0132P4 and
[level calibration](level-calibration.md) when changing those paths.

Profile WT0132P4 for at least five minutes per scenario:

1. Idle synth; record and manually stop a FREE track.
2. Record 1-, 2-, and 8-bar drum tracks at 127 BPM; listen through 20 wraps.
3. Capture a running built-in beat; verify its generator stops exactly once.
4. Play six synchronized tracks while changing selection, volume, mute, solo,
   pause, bars, BPM, and metronomes.
5. Browse raw and patch sounds over a running beat while exercising OLED/input.

Require no underruns, watchdog resets, stuck notes/tracks, drift, doubled beats,
altered transients, or interaction-induced gaps. Performance logs must show
`slow=0`, cycle `over=0`, `looper_over_budget=0`, queue `rejected=0`,
`latency_blocks<=1`, and `snapshot_skipped=0`. Snapshot retries must not produce
torn/regressing UI state. Record revision, board, configuration, duration, worst
log line, and results in the change's review artifacts.

## WT0132P4 wiring

The board header is authoritative for firmware defaults. ES8390 uses codec I2C
SDA GPIO10, SCL GPIO9, address `0x10`; I2S MCLK GPIO6, BCLK GPIO5, WS GPIO3,
TX GPIO4, RX GPIO2. GPIO2 also establishes the codec address at startup.
Audio is 44.1 kHz, 16-bit, with 100% codec startup volume. Controls use a separate
I2C bus on GPIO29/30. The amplifier shutdown signal is GPIO54.

The documented custom line-out wiring uses LOUTP/ROUTP through 0 ohm, 1 uF AC
coupling, and 4.7 ohm series resistors; LOUTN/ROUTN are disconnected. Check the
actual board revision before using these wiring details. For clock diagnosis,
nominal MCLK is 11.2896 MHz, BCLK 1.4112 MHz, and LRCK 44.1 kHz. Use boot
diagnostics and microphone channel peaks to distinguish codec/input issues
from playback issues; do not restore historical DAC-only bring-up settings.
