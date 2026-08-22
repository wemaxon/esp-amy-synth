# Looper real-time architecture and AUD-004 validation

## Contract

The looper keeps its existing six-track public API, sample format, state
transitions, metronome behavior, mixing arithmetic, and UI snapshot contents.
Control commands now take effect at the next audio-block boundary instead of
mutating state from the application task. At 44.1 kHz with 256-frame blocks,
that boundary is at most one nominal 5.804 ms block away while audio is
running.

Auto-bounce is exposed only through `synth_looper_request_auto_bounce()` and
`synth_looper_poll_auto_bounce()`, so the application task never waits for the
audio task. There is no synchronous compatibility facade.

The boundary is strict: looper buffers require PSRAM, BPM must be nonzero, and
auto-bounce bars must be in `1..8`. Invalid configuration is returned to the
caller; it is never substituted with an internal-RAM allocation, 120 BPM, or a
one-bar default.

## Ownership

| Data | Writer | Readers | Exchange |
| --- | --- | --- | --- |
| Track state, sample positions, metronomes, transport state | Audio task | Audio task | Direct, single-owner access |
| Preallocated PSRAM track sample buffers | Initialization, then audio task | Audio task | Stable pointers after initialization; initialization fails if PSRAM is unavailable |
| Control intent | Application task | Audio task | 32-entry bounded SPSC command queue |
| UI/runtime view | Audio task | Application task | Three immutable snapshot slots with reader pins |
| Diagnostics | Producer/audio atomics | Application/profile code | Relaxed counters and maxima |

Initialization finishes all allocations before the audio task starts. No
allocation, formatting, logging, UI work, blocking wait, or shared critical
section is present in `synth_looper_process_audio()`.

The command channel is deliberately SPSC: firmware control calls originate on
the application task and the audio task is the only consumer. A full queue
rejects the new command with `ESP_ERR_NO_MEM`; it never overwrites or reorders
accepted commands. Auto-bounce permits one outstanding request, and its result
is published in the same runtime snapshot as the resulting track state.

The snapshot writer never waits for a reader. It writes only an unpinned,
currently unpublished slot; if both alternatives are pinned, publication for
that block is skipped and the previous coherent snapshot remains visible.

## Reviewable implementation passes

| Pass | Current behavior | Structural improvement | Stability check |
| --- | --- | --- | --- |
| Bound the cross-task channel | UI actions previously changed looper state synchronously under one spinlock. | Public mutation facades enqueue FIFO commands which the audio task applies before processing a block. | Command FIFO/full/wrap/high-water C vectors; command transition and audible hardware matrix below. |
| Make audio the state owner | DSP copied state under the shared lock and reconciled it after the block. | The audio task is the only runtime writer; lock-coupled helper names and every looper critical section are removed. | Structural gate rejects critical sections and stale `_locked` helpers; both target builds must pass. |
| Publish immutable UI state | UI snapshot construction copied and converted live state while holding the audio lock. | Audio publishes raw, coherent views through a triple buffer; the app performs formatting and frame-to-ms conversion after copying. | Snapshot consistency/pinned-slot C vectors plus OLED state/progress parity on hardware. |
| Make bounce asynchronous | Drum auto-bounce synchronously selected a track and returned it to the app. | Request/poll completion carries the audio-owned result; the synchronous API is removed. | Structural gates require the async-only path; exercise success, no-free-track, and cancellation cases. |
| Keep production telemetry | Maximum looper wait/processing risk was unknown. | Lifetime max process time, over-budget count, queue depth/high-water/rejections, command latency, and snapshot contention counters stay enabled. Existing optional AMY performance logs include them. | Stress profile requires zero audio deadline misses/rejections/skipped publications and command latency no greater than one processed block. |

## Hardware profiling gate

No trustworthy legacy lock-hold baseline can be produced without running the
WT0132P4 hardware. The architectural lock hold after this change is zero by
construction; AUD-004 is not considered hardware-closed until the new audio
path is profiled on-device.

Capture at least five minutes per scenario from the `SYNTH_AMY` performance
line:

1. Idle synth and looper.
2. Record a free-length track, stop it manually, and play it back.
3. Record a fixed-bar master, then add a master-synchronized track.
4. Play all six tracks while repeatedly changing selection, volume, mute,
   solo, pause, bars, BPM, and both metronomes.
5. Run the drum auto-bounce flow, including leaving the mode while a bounce
   request is pending.
6. Repeat the heaviest case with the highest practical synth voice load and
   continuous OLED interaction.

Acceptance criteria:

- `slow=0`, `cycle ... over=0`, and `looper_over_budget=0`.
- `looper_queue ... rejected=0` and `latency_blocks<=1`.
- `snapshot_skipped=0`; retries may be nonzero but must not produce torn or
  visibly regressing UI state.
- No clicks, gaps, underruns, watchdog resets, stuck tracks, incorrect bounce
  ownership, or loss/reordering of accepted controls.
- Record/play lengths, count-in, master synchronization, mute/solo/volume,
  metronome state, pause, clear, and OLED progress/message behavior match the
  pre-refactor firmware.

Save the firmware revision, board revision, configuration, scenario duration,
worst log line, and pass/fail notes with the audit. If a gate fails, retain this
ownership model and optimize the bounded audio-owned operation responsible;
do not restore a cross-task looper lock.
