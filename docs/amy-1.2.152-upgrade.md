# AMY 1.2.152 upgrade

AMY is pinned to upstream release `1.2.152` (`aeb0097`), replacing `1.2.4`
(`b796d1d`). This is a dependency migration; it is intentionally separate from
the repository modernization refactors.

## Preserved behavior

- The firmware-facing `synth_amy_*` API is unchanged.
- The patch browser remains limited to the existing patches `0..257`. AMY's
  later reserved range and Gamma9001 sample-backed patches are not exposed by
  this pass.
- Per-role stereo pan still targets each allocated AMY voice independently.
- Delay, performance diagnostics, startup volume, and looper behavior retain
  their existing application-level contracts.

## Integration changes

- The component now compiles AMY's required `cv_trigger.c` source.
- Per-voice pan translates AMY voice ownership to absolute oscillator targets;
  AMY removed the old direct `amy_event.voices` field.
- Performance counters use AMY's separate oscillator `status` and `role`
  fields.
- The stale direct `amy_global.echo` write was removed. Delay configuration
  already supplies `echo_max_delay_ms` through AMY's event API.
- The existing local ESP-IDF task-signature and reserved-osc null-safety fixes
  were carried forward. Local fixes already implemented upstream were dropped.

## Validation gates

- `python tools/chord_engine_spec_check.py`
- `python tools/firmware_refactor_check.py`
- `python tools/host_parity_check.py`
- ESP32-P4 firmware build with ESP-IDF 6.0.1
- ESP32 firmware build with ESP-IDF 6.0.1
- Hardware smoke test: boot, browse representative raw/Juno/DX7/PCM sounds,
  play and release a full chord, toggle stereo and delay, and confirm clean
  audio with no AMY allocation or deadline warnings.

## Separate future migrations

Exposing Gamma9001 patches `384..390`, adopting AMY's additional buses or new
synth features, and rebuilding/distributing the host Python extension are
functional changes and should each remain separate tasks with audio parity
captures and hardware performance measurements.
