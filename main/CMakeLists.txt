idf_component_register(
    SRCS
        "main.c"
        "esp_pocket_audio.c"
        "hmi/control_bus.c"
        "hmi/controls.c"
        "hmi/diagnostic_ui.c"
        "hmi/joystick_calibration.c"
        "hmi/keymap.c"
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
        "."
)
