# esp-pocket-audio Board Component

Custom ESP-ADF board implementation for the planned synth hardware.

The component keeps the ESP32-A1S / ES8388 audio codec and I2S wiring pattern,
then adds a separate control I2C bus for:

- ADS1115 analog inputs, currently joystick X/Y.
- PCF8574 active-low switch inputs.

Edit hardware-specific values in:

- `esp_pocket_audio/esp_pocket_audio_board_config.h`

Important defaults:

- Codec I2C remains on the ESP32-A1S ES8388 bus.
- Control I2C defaults to `I2C_NUM_1`, SDA `GPIO16`, SCL `GPIO17`.
- ADS1115 defaults to address `0x48`.
- PCF8574 defaults to address `0x20`.
- PCF8574 `INT` defaults to `GPIO34`.
- One PCF8574 is enabled by default. Increase `ESP_POCKET_AUDIO_PCF8574_COUNT` to
  `2` when menu switches are wired to a second expander.

The reusable input API is exposed through `esp_pocket_audio_input.h`.
