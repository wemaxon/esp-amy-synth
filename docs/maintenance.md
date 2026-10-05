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

## VS Code devcontainer

Use Docker Desktop and the Dev Containers extension, then choose **Reopen in
Container**. The checked-in container pins ESP-IDF `v6.0.1`, installs the native
GCC/CMake/Ninja tools, and keeps `build/` in a named Docker volume so Windows
and Linux CMake files are never mixed. The container starts as root because the
ESP-IDF image and mounted workspace otherwise produce ownership failures; it is
used only for development, not as a production runtime.

After the container opens, apply or verify AMY explicitly:

```sh
python3 tools/amy_patches.py       # fresh pinned submodule
python3 tools/amy_patches.py --check  # already patched checkout
```

Run **Tasks: Run Task** in VS Code. The useful tasks are `Native: Debug tests`,
`Native: Release tests`, `Tests: all native and repository checks`, `P4: build
firmware (container)`, and `P4: size report (container)`. P4 flashing is not an
automatic task; connect the board and run an explicit `idf.py -p PORT flash`.
The container exposes no serial device by default. Add a deliberate
`--device=/dev/ttyACM0` run argument only when the P4 board is connected under
WSL2/Docker Desktop.

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

### Audio buses

AMY runs two buses (`max_buses = AMY_BUS_COUNT`, see `amy_event_builder.h`).
Bus 0 carries the musical synth: patch synths, raw and PCM voices, and the
user's reverb, delay, chorus and flanger. Bus 1 carries the drums only and has
no effects. Master volume is a per-bus AMY setting and is sent to both buses.

Juno ROM patches end with a global EQ/chorus phrase (`x...k...`); AMY 1.2.172
applies it to the bus of the synth being loaded, so it only colours bus 0. Each
sound change first resets bus 0 to flat EQ and no chorus, so DX7 and raw sounds
do not inherit the previous Juno patch's EQ. When the user's Chorus is on it is
re-sent after every patch load and wins over the patch; when it is off, a Juno
patch's own chorus remains. The flanger runs in AMY's bus post-process hook on
bus 0, after reverb and before the final mix; its time is still reported as
`flanger_us` in the perf log. Drum level calibration predates this split and
was measured through bus 0's effects state; re-run it on hardware.

### Chords

`chord_engine.c` owns pure chord construction, formula/name metadata, inversions,
and bounded voice-leading search. It has no ESP-IDF, AMY, allocation, or I/O
dependency. `app/chord_tools.c` supplies held-button, lock, and bass context;
the coordinator owns edge-triggered key changes. `amy_voice_map.c` alone reduces
the result to six musical voices. Keep note order by musical role, not pitch:
inversions change pitches without rearranging root/third/fifth slots.

The behavioral reference is the [official 2.9 beta manual](https://hichord.github.io/hichord-beta-updater/manual/)
and [companion source](https://app.hichord.shop/app.js?v=20260701a), re-checked
2026-10-05; [features.md](hichord/features.md) mirrors it. Default, Extended,
Chromatic, Borrow, and OG are exposed. Chromatic horizontal gestures transpose
once per direction edge, including held chords. Borrow shifts the chord root,
not the selected key. A Chord Lock stores its key and stays pinned to it; key
steps that carry the global octave (B/C) are cancelled per lock so the locked
pitch does not jump, while deliberate global octave changes still move it.
Existing enum values are retained, with new modes/modifiers appended.
`synth_scale_t` uses the companion app's scale numbering (CC 103).

Public-source limits and deliberate local policies (not verified device parity):

- Default Down-right is a true ninth (seventh plus ninth); on the diminished
  button it plays m7b5, as the 2.9 manual states. Extended add9 remains
  distinct. OG replaces the ninth with the doubled third and keeps the corrected
  names; its diminished variant (m7b5 plus doubled third) is inferred.
- Default Left turns major and minor chords diminished and the diminished
  chord minor. Up (flip) on the diminished chord gives minor, and sus/aug
  directions on it use the same formulas as other chords; the manual does not
  specify these.
- Seven of the ten scales are implemented: Major, Natural/Harmonic/Melodic
  Minor, Dorian, Mixolydian, Lydian. Each button stacks the scale's own third
  and fifth on its degree. Augmented degrees (harmonic/melodic minor III+) take
  the major column for joystick modifiers; this is unpublished. Major
  Pentatonic, Minor Pentatonic and Blues stay reserved in the enum but are
  skipped by the selector and rejected by the engine: their seven-button chords
  are not published (only hint: "Min6: Minor Pentatonic, button 6"). No guessed
  table; capture a real unit's USB MIDI output to add them.
- Per-button and global octave shifts are both -2..+2. Where combined shifts
  exceed MIDI, the root folds down whole octaves before voicing.
- The companion app can set one inversion for all seven buttons (CC 56); the
  device itself only cycles per-button inversions, which is what is implemented.
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
assignment. Pitch-class expectations cover all keys, degrees, modifiers,
inversions, and bass modes; explicit vectors quote the 2.9 manual's examples and
cover names, pitches, scales, Borrow/OG, register limits, pinned locks, slash
bass, and voice leading. Passing these
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
   The beat's tone must not change with the selected sound or effects.

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
