#ifndef SYNTH_AMY_AUDIO_H
#define SYNTH_AMY_AUDIO_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_pocket_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int16_t *(*synth_amy_audio_render_cb_t)(void);
typedef uint16_t (*synth_amy_audio_count_cb_t)(void);
typedef void (*synth_amy_audio_input_cb_t)(const int16_t *interleaved,
                                          uint32_t frames,
                                          uint8_t channels);
typedef bool (*synth_amy_audio_output_mute_cb_t)(void);
typedef const int16_t *(*synth_amy_audio_record_cb_t)(void);
typedef void (*synth_amy_audio_block_cb_t)(void);

typedef struct {
    const esp_pocket_audio_hw_t *hw;
    /* Runs right before each render: events added here (with sample
     * offsets) play inside the block about to be rendered. */
    synth_amy_audio_block_cb_t before_render;
    synth_amy_audio_render_cb_t render;
    /* What the looper records; NULL records the full output block. */
    synth_amy_audio_record_cb_t record_source;
    synth_amy_audio_count_cb_t active_osc_count;
    synth_amy_audio_count_cb_t allocated_patch_osc_count;
    synth_amy_audio_input_cb_t input;
    synth_amy_audio_output_mute_cb_t capture_mutes_output;
} synth_amy_audio_config_t;

/* One rendered block as seen by an audio tap (hardware tests). */
typedef struct {
    uint64_t frame;            /* looper audio-frame index of the block */
    uint32_t frames;
    uint8_t channels;
    const int16_t *output;     /* interleaved, exactly what goes to the DAC */
    const int16_t *synth_bus;  /* interleaved synth bus alone (no drums, no loops) */
} synth_amy_audio_tap_block_t;

typedef void (*synth_amy_audio_tap_t)(const synth_amy_audio_tap_block_t *block);

/* Installs (or with NULL removes) a function called in the audio task after
 * every block is final. It must not block. */
void synth_amy_audio_set_tap(synth_amy_audio_tap_t tap);

uint32_t synth_amy_audio_block_us(void);
esp_err_t synth_amy_audio_start(const synth_amy_audio_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
