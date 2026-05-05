idf_component_register(
    SRCS
        "pocket_oled.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_i2c
)
