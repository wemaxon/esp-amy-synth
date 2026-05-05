# esp-pocket-audio IDF-native hardware support.

idf_component_register(
    SRCS
        "esp_pocket_audio/ads1115.c"
        "esp_pocket_audio/es8388.c"
        "esp_pocket_audio/esp_pocket_audio.c"
        "esp_pocket_audio/esp_pocket_audio_input.c"
        "esp_pocket_audio/pcf8574.c"
    INCLUDE_DIRS
        "esp_pocket_audio"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
)
