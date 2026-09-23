| Active Target | ESP32P4 (WT0132P4-A1) |
| ----------------- | ------------- |

# esp-pocket-synth
IDF-native pocket synthesizer firmware for WT0132P4-A1 hardware.
Ai-Thinker ESP32-A1S is deprecated; its configuration is retained but no longer
hardware-tested.

## Board Selection

Select the hardware board with `idf.py menuconfig` under
`ESP Pocket Audio -> Board configuration`.

The ESP32 target defaults to `ai-thinker-a1s`. The ESP32P4 target defaults to
`wt0132p4-a1`.

## Features

### Diagnostic Mode

Hold `MODE_MENU` while the device boots to enter diagnostic mode. Normal playback
does not start in this mode; the firmware keeps the input service running and
shows live switch and joystick state on the OLED.

The diagnostic screen shows:

- `SW`: chord buttons as `1..7` or `.`, followed by `K`, `M`, `S`, and `J` for
  key, mode, sound, and joystick click.
- Raw ADS1115 joystick X/Y readings.
- Normalized joystick X/Y values, shown as roughly `-100..+100`.
- On microphone-enabled boards, press `MODE_MENU` to switch to `DIAG MIC INPUT`.
  This page shows independent raw left/right I2S peaks, the selected capture
  channel, received-frame count, and RX errors. It also logs one-second peak
  windows and dumps the codec registers once when the page is entered. Press
  `MODE_MENU` again to return to the controls page.
- Joystick direction: `CENTER`, `UP`, `UP_RIGHT`, `RIGHT`, `DOWN_RIGHT`,
  `DOWN`, `DOWN_LEFT`, `LEFT`, or `UP_LEFT`.
- Calibration state: `IDLE`, `RANGE`, `SAVED`, `RESET`, or `ERROR`.

Diagnostic controls:

- Press joystick click while the stick is centered to capture the center point
  and start range capture.
- Move the joystick through its full travel in all directions while the
  calibration state is `RANGE`.
- Press `SOUND_MENU` to save the captured calibration to NVS.
- Press `KEY_MENU` to erase saved calibration and return to board defaults.

OLED updates are queued as low-priority page writes on the control-I2C service.
PCF8574 key scans and ADS1115 joystick reads use high-priority I2C jobs, so
display refreshes should not block input servicing for a full OLED frame.

### Joystick Calibration

Joystick calibration maps raw ADS1115 readings to normalized axis values used by
the joystick direction classifier and chord-quality modifier. Calibration lives
in RAM while running and is persisted in NVS only when saved. The NVS namespace
is `joy_cal` and the key is `data`.

Stored calibration contains:

- X-axis raw minimum, center, maximum, and inversion flag.
- Y-axis raw minimum, center, maximum, and inversion flag.
- Raw deadzone width.
- A magic/version header so incompatible saved data can be ignored.

On boot, the firmware loads saved calibration if it is present and valid. If no
valid saved calibration exists, it uses defaults from
`main/esp_pocket_audio_board_config.h`:

- `ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW`
- `ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW`
- `ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW`
- `ESP_POCKET_AUDIO_ADS1115_DEADZONE_RAW`
- `ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_INVERT`
- `ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_INVERT`

Normalization is center-relative:

- Values within `center_raw +/- deadzone_raw` become `0.0`.
- Positive travel is scaled from `center_raw + deadzone_raw` to `max_raw`.
- Negative travel is scaled from `center_raw - deadzone_raw` to `min_raw`.
- Results are clamped to `-1.0..+1.0`.
- Axis inversion flips the sign after scaling.

Saved calibration is accepted only when `min_raw < center_raw < max_raw` for
both axes. Invalid saved data is ignored and board defaults are used.

### Wheel Input

The logical wheel controls master volume. Holding Key, Sound, or Mode while
turning controls attack, release, or filter cutoff; holding a mixer track
controls that track's volume.

WT0132P4-A1 uses the ESP32-P4 PCNT peripheral for full quadrature decoding:

- Encoder phase A: GPIO17
- Encoder phase B: GPIO18
- Encoder common: GND
- External pull-ups on both phase signals (internal pulls are disabled)
- Four PCNT counts per detent and a fixed 2% change per detent

The encoder is relative to the active target, so selecting another target does
not move it until the encoder is turned. The AI Thinker A1S retains its absolute
ADS1115 wheel and saved range calibration.

### Mic Sample

WT0132P4-A1 uses ES8390 RIN1/ADC2 and full-duplex I2S for the RAM-only Mic
Sample mode. The wheel sets logarithmic `0.5x..10x` mic gain; hold chord button
1 to record up to 3 seconds, then release to pitch-detect, normalize the sample
reference to C, and switch to Lead mode. Unpitched material remains playable as
a raw sample. Wiring, architecture, and hardware validation are documented in
`docs/mic-sampling.md`.

### Voice Architecture

The AMY voice architecture is selected at compile time in
`main/esp_pocket_audio_board_config.h`:

- `ESP_POCKET_AUDIO_AMY_STEREO_PAIRS=0`: mono build, six musical voices and six
  AMY oscillators total.
- `ESP_POCKET_AUDIO_AMY_STEREO_PAIRS=1`: stereo-pair build, six musical voices
  plus six opposite-panned companion oscillators for twelve AMY oscillators
  total.

### Joystick Chord Modes

The chord engine supports five HiChord-inspired joystick mode mappings:

- `DEFAULT`: maj/min flip, dom7, maj7/min7, ninths, sus4, 6/sus2,
  diminished, and augmented.
- `EXTENDED`: maj/min flip, dom7#9, 7sus4, add11, half-dim7, dom9,
  add9, and min11.
- `CHROMATIC`: minMaj7, maj13, half-dim7, 6/9, maj7#11, dom13,
  dom7b9, and dom7alt. Horizontal gestures shift the key once per direction
  edge, including while a chord is held.
- `BORROW` and `OG`: borrowed-root modifiers and the original extended voicings.

In the Sound/FX menu, select `JOYSTICK` and press joystick click to cycle
`DEFAULT -> EXTENDED -> CHROMATIC -> BORROW -> OG`.

Native execution and repository checks (Linux or Ubuntu WSL2):

```sh
python tools/chord_engine_spec_check.py
python tools/firmware_refactor_check.py
python tools/host_parity_check.py
python tools/level_calibration_check.py
```

Build commands, ownership boundaries, compatibility rules, and hardware checks
are in [Firmware maintenance](docs/maintenance.md).
The native-test, headless-simulator and P4 automation roadmap is in the
[Firmware testability plan](docs/testing-plan.md).
The automatic instrument loudness workflow and its real-time constraints are
documented in `docs/level-calibration.md`.

## Prerequisites

#### ESP-IDF
Use ESP-IDF directly.

Initialize the pinned dependencies with `git submodule update --init --recursive`
on a fresh checkout, then run `python3 tools/amy_patches.py` to apply the
repository-managed firmware patch. For an existing patched checkout, use
`python3 tools/amy_patches.py --check` instead; preserve local submodule changes.
The AMY Python package is optional and only needed for the experiments under
`experiments/amy`; it is not required to build or flash the firmware.
