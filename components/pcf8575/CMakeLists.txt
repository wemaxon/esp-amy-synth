idf_component_register(
    SRCS
        "pcf8575.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_i2c
)
