# Looper real-time architecture and AUD-004 validation

## Contract

The looper keeps its six-track public API, mono `int16_t` storage, metronome,
mixing, and UI snapshot contents. Control commands take effect at the next
audio-block boundary. At 44.1 kHz with 256-frame blocks, that is at most one
nominal 5.804 ms block while audio is running.

FREE recording remains manually stopped. Fixed `1..8` bar recording uses an
exact integer frame span derived from sample rate and BPM. Drum-pattern step
deadlines use the same rational timing rule in RTOS ticks, so neither path
accumulates a rounded per-step interval. Recorded samples are preserved
verbatim; the looper does not crossfade or rewrite percussion transients at a
wrap boundary.

A live built-in drum pattern persists across mode changes. If the user records
that generator into a looper track, the application detects the
`RECORDING -> LOOPING` handoff and stops the generator, leaving only the
captured audio. Manual drum-pad recordings do not alter generator state.

## Ownership

| Data | Writer | Readers | Exchange |
| --- | --- | --- | --- |
| Track state, sample positions, metronomes, transport state | Audio task | Audio task | Direct, single-owner access |
| Preallocated PSRAM mono sample buffers | Initialization, then audio task | Audio task | Stable pointers after initialization |
| Control intent | Application task | Audio task | 32-entry bounded SPSC command queue |
| UI/runtime view | Audio task | Application task | Three immutable snapshot slots with reader pins |
| Drum capture completion | Audio snapshot publisher | Application task | Track-state transition detector |
| Diagnostics | Producer/audio atomics | Application/profile code | Relaxed counters and maxima |

There is one authoritative `synth_looper_track_t` per track. The previous
per-buffer shadow copy and end-of-buffer reconciliation are gone. No
allocation, formatting, logging, UI work, blocking wait, or shared critical
section is present in `synth_looper_process_audio()`.

The command channel is SPSC: firmware control calls originate on the
application task and the audio task is the only consumer. A full queue rejects
the new command with `ESP_ERR_NO_MEM`; accepted commands are not overwritten
or reordered. The snapshot writer never waits for a reader. If both alternate
slots are pinned, publication for that block is skipped and the previous
coherent snapshot remains visible.

## Regression checks

| Behavior | Structural improvement | Stability check |
| --- | --- | --- |
| Sound-menu joystick input changes the selected instrument without becoming a drum command. | One explicit priority route owns each joystick update. | Control-route vectors cover Sound menu over Drum mode and normal Drum routing. |
| Instrument changes release musical voices but do not stop or reconfigure drum hits. | Musical and drum oscillator ownership is separate; drum oscillators are excluded from AMY patch allocation. | Structural AMY ownership gate plus device test while cycling raw and patch sounds over a running beat. |
| Fixed-bar recordings wrap on the musical grid. | Drum steps and looper lengths share rational transport arithmetic. | Exact 127 BPM step/bar and long-run timing vectors. |
| Captured PCM remains the audio that was recorded. | Removed seam conditioning and isolated pure track record/play state. | Raw-capture, exact-wrap, muted-position, and exact-target C vectors. |
| Capturing a live built-in beat does not leave a doubled generator. | Small snapshot transition coordinator stops it only after successful capture. | Completion, manual-recording, and cancellation vectors. |
| Switch, joystick, and wheel readers see coherent values. | The control service publishes complete state under a task-level state mux. | Target build plus device input stress test. |

## Hardware profiling gate

AUD-004 remains hardware-validation pending until the WT0132P4 is profiled.
Capture at least five minutes per scenario from the `SYNTH_AMY` performance
line:

1. Idle synth and looper.
2. Record and manually stop a FREE track.
3. Record fixed 1-, 2-, and 8-bar drum-pad tracks at awkward BPM values such
   as 127, then listen through at least 20 wraps.
4. Capture a running built-in drum pattern and verify the live generator stops
   exactly once after the track enters playback.
5. Add master-synchronized tracks, then play all six while changing selection,
   volume, mute, solo, pause, bars, BPM, and metronomes.
6. Keep a beat running while cycling every raw and patch instrument in the
   Sound menu and operating the OLED and joystick continuously.

Acceptance criteria:

- `slow=0`, `cycle ... over=0`, and `looper_over_budget=0`.
- `looper_queue ... rejected=0` and `latency_blocks<=1`.
- `snapshot_skipped=0`; retries may be nonzero but must not produce torn or
  visibly regressing UI state.
- Drum timbre and rhythm do not change during sound browsing.
- Fixed recordings have no periodic drift, doubled downbeat, altered first
  transient, or short/long wrap.
- FREE recording, count-in, master synchronization, mute/solo/volume,
  metronome state, pause, clear, and OLED progress/message behavior remain
  stable.
- No underruns, watchdog resets, stuck tracks, clicks, or gaps attributable to
  control interaction.

Save the firmware revision, board revision, configuration, scenario duration,
worst log line, and pass/fail notes with the audit. If a gate fails, retain the
single-owner model and optimize the bounded operation responsible; do not
restore a cross-task looper lock or destructive seam processing.
