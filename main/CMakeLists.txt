idf_component_register(
    SRCS
        "main.c"
        "diagnostics.c"
        "esp_pocket_audio.c"
        "esp_pocket_audio_input.c"
        "esp_pocket_audio_joystick.c"
        "hichord_app.c"
        "hichord_synth_amy.c"
        "hichord_theory.c"

    REQUIRES
        ads1115
        amy
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
