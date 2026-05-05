idf_component_register(
    SRCS
        "main.c"
        "diagnostics.c"
        "esp_pocket_audio.c"
        "esp_pocket_audio_input.c"
        "esp_pocket_audio_joystick.c"

    REQUIRES
        ads1115
        es8388
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        nvs_flash
        pcf8574
        pocket-oled

    INCLUDE_DIRS
        ""
)
