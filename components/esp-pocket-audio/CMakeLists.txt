# esp-pocket-audio custom ESP-ADF audio board.

set(COMPONENT_REQUIRES)
set(COMPONENT_PRIV_REQUIRES audio_sal audio_hal esp_dispatcher esp_peripherals display_service driver)

if(CONFIG_AUDIO_BOARD_CUSTOM AND CONFIG_AUDIO_BOARD_ESP_POCKET_AUDIO)
message(STATUS "Current board name is esp-pocket-audio")
list(APPEND COMPONENT_ADD_INCLUDEDIRS ./esp_pocket_audio)
set(COMPONENT_SRCS
    ./esp_pocket_audio/ads1115.c
    ./esp_pocket_audio/board.c
    ./esp_pocket_audio/board_pins_config.c
    ./esp_pocket_audio/esp_pocket_audio_input.c
    ./esp_pocket_audio/pcf8574.c
)
endif()

register_component()

if(IDF_VERSION_MAJOR GREATER 3)
idf_component_get_property(audio_board_lib audio_board COMPONENT_LIB)
set_property(TARGET ${audio_board_lib} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${COMPONENT_LIB})
elseif(IDF_VERSION_MAJOR EQUAL 3)
set_property(TARGET idf_component_audio_board APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
endif()
