set(ESP_POCKET_AUDIO_BOARD "ai-thinker-a1s" CACHE STRING "Pocket audio board config")
set_property(CACHE ESP_POCKET_AUDIO_BOARD PROPERTY STRINGS "ai-thinker-a1s" "wt0132p4-a1")

string(TOLOWER "${ESP_POCKET_AUDIO_BOARD}" ESP_POCKET_AUDIO_BOARD_SELECTED)
if(ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "ai-thinker-a1s"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "ai_thinker_a1s"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "esp_pocket_audio_board_ai_thinker_a1s"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "1")
    set(ESP_POCKET_AUDIO_BOARD_DEFINE ESP_POCKET_AUDIO_BOARD=ESP_POCKET_AUDIO_BOARD_AI_THINKER_A1S)
elseif(ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "wt0132p4-a1"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "wt0132p4_a1"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "esp_pocket_audio_board_wt0132p4_a1"
        OR ESP_POCKET_AUDIO_BOARD_SELECTED STREQUAL "2")
    set(ESP_POCKET_AUDIO_BOARD_DEFINE ESP_POCKET_AUDIO_BOARD=ESP_POCKET_AUDIO_BOARD_WT0132P4_A1)
else()
    message(FATAL_ERROR "Unsupported ESP_POCKET_AUDIO_BOARD: ${ESP_POCKET_AUDIO_BOARD}")
endif()

idf_component_register(
    SRCS
        "main.c"
        "diagnostics/amy_patch_browser.c"
        "diagnostics/diagnostic_ui.c"
        "diagnostics/i2c_scan.c"
        "diagnostics/sine_output.c"
        "esp_pocket_audio.c"
        "hal/audio_codec.c"
        "hmi/control_i2c.c"
        "hmi/controls.c"
        "hmi/joystick_calibration.c"
        "hmi/keymap.c"
        "hmi/oled_display.c"
        "hmi/play_ui.c"
        "hmi/switch_expander.c"
        "hmi/wheel_calibration.c"
        "synth/app.c"
        "synth/amy_engine.c"
        "synth/chord_engine.c"
        "synth/looper.c"
        "synth/play_mode.c"
        "synth/sound.c"

    REQUIRES
        ads1115
        amy
        es8388
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        nvs_flash
        pcf8574
        pcf8575
        pocket-oled

    INCLUDE_DIRS
        "."
)

message(STATUS "Pocket audio board: ${ESP_POCKET_AUDIO_BOARD}")
target_compile_definitions(${COMPONENT_LIB} PUBLIC ${ESP_POCKET_AUDIO_BOARD_DEFINE})
