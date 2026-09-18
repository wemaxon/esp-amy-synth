# Microphone sampling

The WT0132P4-A1 build implements the HiChord Mic Sample workflow as a volatile,
RAM-only feature. Select `MIC SAMPLE`, turn the wheel to set input gain, then
hold chord button 1 while recording. Releasing button 1 stops capture; capture
also stops automatically at 3 seconds. The firmware analyzes the sample,
installs it into AMY, and switches to `LEAD` mode.

If pitch detection is confident, the detected pitch becomes the AMY sample's
reference note, so the recording is transposed canonically to C and the chord
buttons play it chromatically. If pitch detection is not confident, the same
audio is installed untransposed and the UI reports `MIC RAW READY`.

Samples are not persisted. Resetting or powering off the device discards them.
Flash/NVS persistence is intentionally a separate migration task because it
requires a storage format, capacity policy, versioning, and write-lifetime
decisions.

## Hardware path

The configured path is:

`mic module -> ES8390 RIN1 -> PGA2/ADC2 -> ASDOUT -> ESP32-P4 GPIO2 -> I2S RX`

RIN1 is a codec analog input, not an ESP32 GPIO. Connect the biased analog
module output to RIN1 and use a common ground. The module output must remain
within the ES8390 input limits and match the board's input coupling/bias
network. The firmware receives the ADC2 signal from the right I2S slot; the
left slot is deliberately ignored and is covered by host parity tests.

The ES8390 input configuration is explicit: RIN1 single-ended input, PGA2,
ADC2, ADC reference, and only the right serial ADC channel unmuted. ALC is not
enabled. The wheel covers `0.5x..10x` logarithmically. Gain is split between the
largest non-overshooting ES8390 PGA step and a small fixed-point digital
remainder.

## Real-time ownership

- `amy_audio.c` owns full-duplex I2S pacing and drains RX once per AMY block.
- `mic_sample.c` owns capture state and the preallocated 16-bit mono PSRAM
  buffer.
- `mic_sample_dsp.c` owns channel extraction, fixed-point gain, saturation, and
  peak/clip metering.
- `pitch_detector.c` runs bounded YIN analysis outside the audio task.
- `amy_engine.c` installs the finalized sample into alternating RAM PCM preset
  IDs, so a failed allocation does not replace the currently playable sample.

The audio callback performs no allocation, logging, I2C operation, or blocking
lock acquisition. During recording the DAC block is zeroed to prevent speaker
feedback while RX capture and looper transport continue.

## Validation and hardware check

Run the portable checks and target build:

```powershell
python tools/host_parity_check.py
idf.py build
```

On hardware, verify:

1. At `1.0x`, record a stable sung or generated note for at least 0.5 seconds.
2. Confirm the display shows recording progress and a nonzero peak without a
   growing clip count.
3. Release button 1 and confirm the device enters Lead mode.
4. Confirm the log reports `pitch=detected` for a stable tone, or `pitch=raw`
   for noise/unpitched material.
5. Play button 1 and compare its output to C; play other buttons to verify
   chromatic transposition.
6. Repeat at `0.5x` and `10.0x`; clipping must be reported rather than wrapping.

The periodic `SYNTH_AMY` performance log includes `input_us avg/max` and
`errors`. RX errors must remain zero, and the overall cycle must stay within
the AMY block budget. The final `SYNTH_APP` capture log reports frame count,
peak, clipped samples, detected frequency, and pitch confidence.
