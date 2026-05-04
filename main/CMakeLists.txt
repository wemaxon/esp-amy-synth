idf_component_register(
    SRCS
        "app_beat.c"
        "app_synth.c"
        "main.c"

    REQUIRES
        amy
        audio_pipeline
        audio_stream
        audio_board
        audio_hal
        esp_peripherals
        input_key_service
        nvs_flash
        esp_wifi

    INCLUDE_DIRS
        ""
        "include"
)
