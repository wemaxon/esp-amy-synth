# my-synth-v1 custom ESP-ADF audio board.

set(COMPONENT_REQUIRES)
set(COMPONENT_PRIV_REQUIRES audio_sal audio_hal esp_dispatcher esp_peripherals display_service driver)

if(CONFIG_AUDIO_BOARD_CUSTOM AND CONFIG_AUDIO_BOARD_MY_SYNTH_V1)
message(STATUS "Current board name is my-synth-v1")
list(APPEND COMPONENT_ADD_INCLUDEDIRS ./my_synth_v1)
set(COMPONENT_SRCS
    ./my_synth_v1/ads1115.c
    ./my_synth_v1/board.c
    ./my_synth_v1/board_pins_config.c
    ./my_synth_v1/my_synth_v1_input.c
    ./my_synth_v1/pcf8574.c
)
endif()

register_component()

if(IDF_VERSION_MAJOR GREATER 3)
idf_component_get_property(audio_board_lib audio_board COMPONENT_LIB)
set_property(TARGET ${audio_board_lib} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${COMPONENT_LIB})
elseif(IDF_VERSION_MAJOR EQUAL 3)
set_property(TARGET idf_component_audio_board APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
endif()
