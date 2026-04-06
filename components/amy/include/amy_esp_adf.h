#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "audio_element.h"
#include "audio_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool default_synths;
    bool startup_bleep;
    bool multithread;
    bool multicore;
    uint8_t default_synth;
    float master_volume;
} amy_esp_adf_config_t;

amy_esp_adf_config_t amy_esp_adf_default_config(void);

esp_err_t amy_esp_adf_init(const amy_esp_adf_config_t *config);

audio_element_handle_t amy_esp_adf_stream_init(const char *tag);

audio_element_handle_t amy_esp_adf_stream_init_for_core(const char *tag, int task_core, int task_prio);

audio_hal_iface_samples_t amy_esp_adf_audio_hal_sample_rate(void);

int amy_esp_adf_sample_rate(void);

int amy_esp_adf_channels(void);

esp_err_t amy_esp_adf_configure_synth(uint8_t synth, uint16_t patch_number, uint16_t num_voices);

esp_err_t amy_esp_adf_configure_dreamy_pad_synth(uint8_t synth, uint16_t num_voices);

esp_err_t amy_esp_adf_set_fx(float reverb_level, float chorus_level, float echo_level);

esp_err_t amy_esp_adf_set_master_volume(float master_volume);

esp_err_t amy_esp_adf_note_on(uint8_t synth, uint8_t note, float velocity, uint32_t delay_ms);

esp_err_t amy_esp_adf_note_off(uint8_t synth, uint8_t note, uint32_t delay_ms);

esp_err_t amy_esp_adf_schedule_note(uint8_t synth, uint8_t note, float velocity, uint16_t duration_ms, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
