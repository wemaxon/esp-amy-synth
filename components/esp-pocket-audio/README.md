# esp-pocket-audio Hardware Component

IDF-native hardware support for the esp-pocket-audio synth board.

The component owns:

- ES8388 codec register initialization over the ESP32-A1S codec I2C bus.
- I2S clock/output pin setup through the IDF I2S standard driver.
- ADS1115 analog inputs, currently joystick X/Y.
- PCF8574 active-low switch inputs.
- Direct GPIO switch inputs.

Edit hardware-specific values in:

- `esp_pocket_audio/esp_pocket_audio_board_config.h`

Important defaults:

- Codec I2C is `I2C_NUM_0`, SDA `GPIO33`, SCL `GPIO32`.
- Control I2C is `I2C_NUM_1`, SDA `GPIO21`, SCL `GPIO22`.
- ADS1115 defaults to address `0x48`.
- PCF8574 defaults to address `0x20`.
- PCF8574 `INT` is `GPIO18`.
- Chord buttons 1-7 and menu use PCF pins P0-P7.
- Tempo, sound, and joystick click use GPIO19, GPIO23, and GPIO0.

The reusable input API is exposed through `esp_pocket_audio_input.h`.
