set(AMY_SRC_DIR amy/src)

idf_component_register(
    SRCS
        "${AMY_SRC_DIR}/algorithms.c"
        "${AMY_SRC_DIR}/amy.c"
        "${AMY_SRC_DIR}/amy_midi.c"
        "${AMY_SRC_DIR}/api.c"
        "${AMY_SRC_DIR}/custom.c"
        "${AMY_SRC_DIR}/cv_trigger.c"
        "${AMY_SRC_DIR}/delay.c"
        "${AMY_SRC_DIR}/envelope.c"
        "${AMY_SRC_DIR}/filters.c"
        "${AMY_SRC_DIR}/i2s.c"
        "${AMY_SRC_DIR}/instrument.c"
        "${AMY_SRC_DIR}/interp_partials.c"
        "${AMY_SRC_DIR}/log2_exp2.c"
        "${AMY_SRC_DIR}/midi_mappings.c"
        "${AMY_SRC_DIR}/oscillators.c"
        "${AMY_SRC_DIR}/parse.c"
        "${AMY_SRC_DIR}/patches.c"
        "${AMY_SRC_DIR}/pcm.c"
        "${AMY_SRC_DIR}/sequencer.c"
        "${AMY_SRC_DIR}/transfer.c"

    REQUIRES
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_uart
        esp_timer
        heap

    INCLUDE_DIRS
        "${AMY_SRC_DIR}"
)

option(ESP_POCKET_AUDIO_AMY_PROFILE "Build AMY with internal profiling counters" OFF)
# Use AMY's supported block-size setting for both the library and its consumers.
# Keep the existing 256-frame audio/looper timing contract.
target_compile_definitions(${COMPONENT_LIB} PUBLIC BLOCK_SIZE_BITS=8)
if(ESP_POCKET_AUDIO_AMY_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AMY_DEBUG)
endif()
