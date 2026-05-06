| Supported Targets | ESP32 |
| ----------------- | ----- |

# esp-pocket-synth
IDF-native firmware for the esp-pocket-audio ESP32-A1S hardware.

## Features

### Diagnostic Mode

Hold `KEY_MODE` while the device boots to enter diagnostic mode. Normal playback
does not start in this mode; the firmware keeps the input service running and
shows live switch and joystick state on the OLED.

The diagnostic screen shows:

- `SW`: chord buttons as `1..7` or `.`, followed by `T`, `M`, `S`, and `J` for
  tempo, mode, sound, and joystick click.
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
- Press `KEY_SOUND` to save the captured calibration to NVS.
- Press `KEY_TEMPO` to erase saved calibration and return to board defaults.

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
