# Firmware maintenance

## Build and check

Use the pinned AMY submodule and an ESP-IDF environment (currently validated
with 6.0.1). Do not switch dependency revisions as part of a firmware build.
Keep target configurations separate; from the repository root:

```sh
idf.py -B build-esp32 -D SDKCONFIG=build-esp32/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32 -D IDF_TARGET=esp32 build
idf.py -B build-esp32p4 -D SDKCONFIG=build-esp32p4/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32p4 -D IDF_TARGET=esp32p4 build
python tools/chord_engine_spec_check.py
python tools/firmware_refactor_check.py
python tools/host_parity_check.py
python tools/level_calibration_check.py
```

Host parity uses Espressif clang to compile production C into optimized LLVM
vectors. Set `ESP_CLANG` to the compiler path if automatic discovery fails.
Audio-meter and pitch-detector suites are compile checks, not runtime tests.
Device boot checks live in `pytest_firmware.py` and require connected hardware.
Compare build warnings, binary size, RAM use, and relevant device measurements
before and after behavior-sensitive changes.

## Ownership and compatibility

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
