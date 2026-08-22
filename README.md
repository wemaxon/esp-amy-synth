| Supported Targets | ESP32, ESP32P4 |
| ----------------- | ------------- |

# esp-pocket-synth
IDF-native firmware for the esp-pocket-audio ESP32-A1S hardware.

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

The firmware has a logical wheel input used by playback code for volume and
filter cutoff. It is disabled by default until the hardware is wired. Enable and
map it in `main/esp_pocket_audio_board_config.h` with:

- `ESP_POCKET_AUDIO_WHEEL_ENABLED`
- `ESP_POCKET_AUDIO_ADS1115_WHEEL_CH`
- `ESP_POCKET_AUDIO_WHEEL_MIN_RAW`
- `ESP_POCKET_AUDIO_WHEEL_MAX_RAW`
- `ESP_POCKET_AUDIO_WHEEL_INVERT`

### Voice Architecture

The AMY voice architecture is selected at compile time in
`main/esp_pocket_audio_board_config.h`:

- `ESP_POCKET_AUDIO_AMY_STEREO_PAIRS=0`: mono build, six musical voices and six
  AMY oscillators total.
- `ESP_POCKET_AUDIO_AMY_STEREO_PAIRS=1`: stereo-pair build, six musical voices
  plus six opposite-panned companion oscillators for twelve AMY oscillators
  total.

### Joystick Chord Modes

The chord engine supports the three HiChord joystick mode mappings:

- `DEFAULT`: maj/min flip, dom7, maj7/min7, add9, sus4, 6/sus2,
  diminished, and augmented.
- `EXTENDED`: maj/min flip, dom7#9, 7sus4, add11, half-dim7, dom9,
  add9, and min11.
- `CHROMATIC`: minMaj7, maj13, half-dim7, 6/9, maj7#11, dom13,
  dom7b9, and dom7alt. With no chord held, chromatic-mode joystick
  left/right shifts the key by semitone.

In the Sound/FX menu, select `JOYSTICK` and press joystick click to cycle
`DEFAULT -> EXTENDED -> CHROMATIC`.

Fast source-level chord checks:

```sh
python tools/chord_engine_spec_check.py
python tools/firmware_refactor_check.py
python tools/host_parity_check.py
```

The modernization baseline, compatibility boundaries, parity gates, and bug
audit live in `docs/refactor-baseline.md` and `docs/bug-audit.md`. The looper
ownership model and AUD-004 hardware profile are in
`docs/looper-realtime-architecture.md`.

## Prereqisites

#### ESP-IDF
Use ESP-IDF directly.

#### Python environment
```sh
python -m venv .venv
.\.venv\Scripts\activate
cd components\amy\amy
git fetch --tags
git checkout tags/1.2.4
```

#### Install amy python library
```
cd components/amy/amy

git fetch --tags
git checkout tags/1.2.4

pip install -r requirements.txt
pip install -e .
```

## ToDo
- Validate remaining chord-engine edge cases against hardware/manual examples.

- Implement the hichord calibration flow
- Wire and validate the ADS1115 wheel hardware (`ESP_POCKET_AUDIO_WHEEL_ENABLED`)
- Add microphone feature
- Beautify UI
- Performance
  - IRAM
  - PSRAM pools
  - amy multicore/multithread
  - profiling

- Fix sounds stopping playing to early
- Fix volume levels of drums and everything
- Fix octaves
