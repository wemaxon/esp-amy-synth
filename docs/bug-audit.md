# Bug audit

Modernization passes also look for defects.  Findings are kept separate from
behavior-preserving edits so a refactor cannot silently change behavior.

## Workflow

Each finding records its severity, reproduction or evidence, affected targets,
current behavior, intended behavior, and regression check.  Confirmed fixes
land as separate commits or review passes.  Memory-safety, data-loss, and
hardware-safety findings block the dependent refactor until resolved.

## Current findings

| ID | State | Severity | Finding | Disposition |
| --- | --- | --- | --- | --- |
| AUD-001 | Confirmed | Medium | The former root pytest suite was an ESP-IDF hello-world template and asserted logs the firmware never emits. | Replaced with a firmware-entry smoke test; no runtime code changed. |
| AUD-002 | Fixed | Low | Invalid calibration blobs could be logged with `ESP_OK`, obscuring size and validation failures. | Shared persistence now returns `ESP_ERR_INVALID_SIZE` or `ESP_ERR_INVALID_STATE`; fallback data and stored schemas are unchanged. |
| AUD-003 | Characterized | Medium | A full control-event queue discards the oldest event; its effective capacity is 15 events. | Behavior is now isolated and parity-tested. The private queue records overwrite count; changing overflow policy remains a separate functional task. |
| AUD-004 | Hardware validation pending | High | The shared UI/audio looper critical section has been removed; real-device audio-path timing and parity are not yet measured. | Audio now solely owns mutable looper state, with a bounded command queue, triple-buffered snapshots, lifetime counters, and profile logs. Close only after the WT0132P4 stress matrix in `looper-realtime-architecture.md` passes. |
| AUD-005 | Fixed | High | The AI Thinker A1S target referenced a codec-volume macro that existed only in the ESP32-P4 board profile, so a clean ESP32 build failed. | The A1S profile now explicitly preserves its former `100%` codec startup volume; clean ESP32 and ESP32-P4 builds are regression gates. |
| AUD-006 | Fixed | Low | Target-disabled amplifier and AMY performance helpers were still compiled, producing dead-code warnings on ESP32. | Target feature guards now exclude the private helpers while the public amplifier API and runtime behavior remain unchanged. |
| AUD-007 | Fixed | Medium | A looper preallocation failure left `s_initialized` true with only some track buffers available, so later calls could enter an unusable partial state. | Initialization now requires PSRAM, frees partial allocations, remains uninitialized, and returns the allocation error; hardware fault injection remains optional. |

## Audit checklist

- Bounds and enum validation at public boundaries
- Tick/frame counter wraparound and integer overflow
- Allocation failures and cleanup after partial initialization
- Cross-task lock coverage and audio-path lock duration
- Lost, duplicated, or reordered input events
- NVS schema, size, validation, and reset behavior
- I2C byte order, latch state after errors, and handle lifetime
- Voice/oscillator ownership and note-off cleanup
- Ignored `esp_err_t` values and misleading diagnostics
- Differences between ESP32 and ESP32-P4 board configurations
