#include <string.h>

#include "amy_esp_adf.h"

#include "amy.h"

typedef struct {
    bool started;
    uint8_t default_synth;
} amy_esp_adf_state_t;

static amy_esp_adf_state_t s_state = {
    .started = false,
    .default_synth = 1,
};

amy_esp_adf_config_t amy_esp_adf_default_config(void)
{
    amy_esp_adf_config_t config = {
        .default_synths = true,
        .startup_bleep = true,
        .multithread = false,
        .multicore = false,
        .default_synth = 1,
        .master_volume = 8.0f,
    };
    return config;
}

audio_hal_iface_samples_t amy_esp_adf_audio_hal_sample_rate(void)
{
    return (AMY_SAMPLE_RATE == 44100) ? AUDIO_HAL_44K_SAMPLES : AUDIO_HAL_48K_SAMPLES;
}

int amy_esp_adf_sample_rate(void)
{
    return AMY_SAMPLE_RATE;
}

int amy_esp_adf_channels(void)
{
    return AMY_NCHANS;
}

esp_err_t amy_esp_adf_init(const amy_esp_adf_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    amy_config_t amy_config = amy_default_config();
    amy_config.audio = AMY_AUDIO_IS_NONE;
    amy_config.midi = AMY_MIDI_IS_NONE;
    amy_config.features.default_synths = config->default_synths ? 1 : 0;
    amy_config.features.startup_bleep = config->startup_bleep ? 1 : 0;
    amy_config.platform.multithread = config->multithread ? 1 : 0;
    amy_config.platform.multicore = config->multicore ? 1 : 0;

    amy_start(amy_config);
    s_state.started = true;
    s_state.default_synth = config->default_synth;

    amy_event volume_event = amy_default_event();
    volume_event.volume = config->master_volume;
    amy_add_event(&volume_event);

    return ESP_OK;
}

static esp_err_t amy_esp_adf_stream_open(audio_element_handle_t self)
{
    audio_element_set_music_info(self, AMY_SAMPLE_RATE, AMY_NCHANS, 16);
    return ESP_OK;
}

static int amy_esp_adf_stream_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    (void)self;
    (void)ticks_to_wait;
    (void)context;

    int16_t *block = amy_update();
    int block_bytes = AMY_BLOCK_SIZE * AMY_NCHANS * (int)sizeof(int16_t);
    int bytes_to_copy = len < block_bytes ? len : block_bytes;

    memcpy(buffer, block, bytes_to_copy);
    return bytes_to_copy;
}

static int amy_esp_adf_stream_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int read_size = audio_element_input(self, in_buffer, in_len);
    if (read_size <= 0) {
        return read_size;
    }

    return audio_element_output(self, in_buffer, read_size);
}

audio_element_handle_t amy_esp_adf_stream_init(const char *tag)
{
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = amy_esp_adf_stream_open;
    cfg.read = amy_esp_adf_stream_read;
    cfg.process = amy_esp_adf_stream_process;
    cfg.buffer_len = AMY_BLOCK_SIZE * AMY_NCHANS * (int)sizeof(int16_t);
    cfg.out_rb_size = cfg.buffer_len * 4;
    cfg.task_stack = 4096;
    cfg.tag = (tag != NULL) ? tag : "amy";
    return audio_element_init(&cfg);
}

esp_err_t amy_esp_adf_schedule_note(uint8_t synth, uint8_t note, float velocity, uint16_t duration_ms, uint32_t delay_ms)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t target_synth = (synth == 0) ? s_state.default_synth : synth;
    uint32_t start_time = amy_sysclock() + delay_ms;

    amy_event note_on = amy_default_event();
    note_on.time = start_time;
    note_on.synth = target_synth;
    note_on.midi_note = note;
    note_on.velocity = velocity;
    amy_add_event(&note_on);

    amy_event note_off = amy_default_event();
    note_off.time = start_time + duration_ms;
    note_off.synth = target_synth;
    note_off.midi_note = note;
    note_off.velocity = 0.0f;
    amy_add_event(&note_off);

    return ESP_OK;
}
