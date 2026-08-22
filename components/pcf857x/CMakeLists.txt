idf_component_register(
    SRCS
        "pcf857x.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_i2c
)

