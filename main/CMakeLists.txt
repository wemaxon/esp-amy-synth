idf_component_register(
    SRCS
        "main.c"

    REQUIRES
        amy
        audio_pipeline
        audio_stream
        audio_board
        audio_hal
        esp_peripherals
        nvs_flash
        esp_wifi

    INCLUDE_DIRS
        ""
)
