idf_component_register(
    SRCS
        "main.c"
        "diagnostics.c"

    REQUIRES
        es8388
        esp-pocket-audio
        nvs_flash
        pocket-oled

    INCLUDE_DIRS
        ""
)
