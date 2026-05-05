# esp-pocket-audio IDF-native hardware support.

idf_component_register(
    SRCS
        "esp_pocket_audio/esp_pocket_audio.c"
        "esp_pocket_audio/esp_pocket_audio_input.c"
    INCLUDE_DIRS
        "esp_pocket_audio"
    REQUIRES
        ads1115
        es8388
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        pcf8574
)
