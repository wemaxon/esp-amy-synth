set(AMY_SRCS
    src/amy_esp_adf.c
    amy/src/algorithms.c
    amy/src/amy.c
    amy/src/amy_midi.c
    amy/src/api.c
    amy/src/custom.c
    amy/src/delay.c
    amy/src/envelope.c
    amy/src/examples.c
    amy/src/filters.c
    amy/src/i2s.c
    amy/src/instrument.c
    amy/src/interp_partials.c
    amy/src/log2_exp2.c
    amy/src/midi_mappings.c
    amy/src/oscillators.c
    amy/src/parse.c
    amy/src/patches.c
    amy/src/pcm.c
    amy/src/sequencer.c
    amy/src/transfer.c
)

idf_component_register(
    SRCS ${AMY_SRCS}
    INCLUDE_DIRS include amy/src
    REQUIRES driver audio_pipeline audio_stream audio_board audio_hal esp_peripherals
)
