#include <string.h>

#include "amy_esp_adf.h"

#include "amy.h"

typedef struct {
    bool started;
    uint8_t default_synth;
    float master_volume;
} amy_esp_adf_state_t;

static amy_esp_adf_state_t s_state = {
    .started = false,
    .default_synth = 1,
    .master_volume = 1.0f,
};

amy_esp_adf_config_t amy_esp_adf_default_config(void)
{
    amy_esp_adf_config_t config = {
        .default_synths = true,
        .startup_bleep = true,
        .multithread = false,
        .multicore = false,
        .default_synth = 1,
        .master_volume = 1.0f,
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
    s_state.master_volume = config->master_volume;

    return amy_esp_adf_set_master_volume(config->master_volume);
}

esp_err_t amy_esp_adf_set_master_volume(float master_volume)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event volume_event = amy_default_event();
    volume_event.volume = master_volume;
    amy_add_event(&volume_event);
    s_state.master_volume = master_volume;

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
    return amy_esp_adf_stream_init_for_core(tag, 0, DEFAULT_ELEMENT_TASK_PRIO);
}

audio_element_handle_t amy_esp_adf_stream_init_for_core(const char *tag, int task_core, int task_prio)
{
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = amy_esp_adf_stream_open;
    cfg.read = amy_esp_adf_stream_read;
    cfg.process = amy_esp_adf_stream_process;
    cfg.buffer_len = AMY_BLOCK_SIZE * AMY_NCHANS * (int)sizeof(int16_t);
    cfg.out_rb_size = cfg.buffer_len * 4;
    cfg.task_stack = 4096;
    cfg.task_core = task_core;
    cfg.task_prio = task_prio;
    cfg.tag = (tag != NULL) ? tag : "amy";
    return audio_element_init(&cfg);
}

esp_err_t amy_esp_adf_configure_synth(uint8_t synth, uint16_t patch_number, uint16_t num_voices)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.synth = synth;
    event.patch_number = patch_number;
    event.num_voices = num_voices;
    amy_add_event(&event);

    return ESP_OK;
}

esp_err_t amy_esp_adf_configure_dreamy_pad_synth(uint8_t synth, uint16_t num_voices)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t patch_number = 1030;

    amy_event event = amy_default_event();
    event.patch_number = patch_number;
    event.reset_osc = RESET_PATCH;
    amy_add_event(&event);

    event = amy_default_event();
    event.patch_number = patch_number;
    event.osc = 0;
    event.wave = TRIANGLE;
    event.amp_coefs[COEF_VEL] = 0.52f;
    event.amp_coefs[COEF_EG0] = 1.0f;
    event.freq_coefs[COEF_CONST] = 130.81f;
    event.freq_coefs[COEF_NOTE] = 1.0f;
    event.filter_type = FILTER_LPF;
    event.filter_freq_coefs[COEF_CONST] = 420.0f;
    event.filter_freq_coefs[COEF_NOTE] = 0.92f;
    event.filter_freq_coefs[COEF_EG1] = 0.75f;
    event.resonance = 0.95f;
    event.eg0_times[0] = 40;    event.eg0_values[0] = 1.0f;
    event.eg0_times[1] = 900;   event.eg0_values[1] = 0.60f;
    event.eg0_times[2] = 900;   event.eg0_values[2] = 0.0f;
    event.eg1_times[0] = 55;    event.eg1_values[0] = 1.0f;
    event.eg1_times[1] = 700;   event.eg1_values[1] = 0.25f;
    event.eg1_times[2] = 850;   event.eg1_values[2] = 0.0f;
    amy_add_event(&event);

    event = amy_default_event();
    event.synth = synth;
    event.patch_number = patch_number;
    event.num_voices = num_voices;
    amy_add_event(&event);

    return ESP_OK;
}

esp_err_t amy_esp_adf_set_synth_filter_shape(uint8_t synth, float base_freq_hz, float note_tracking, float env_amount, float resonance)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.synth = synth;
    event.filter_freq_coefs[COEF_CONST] = base_freq_hz;
    event.filter_freq_coefs[COEF_NOTE] = note_tracking;
    event.filter_freq_coefs[COEF_EG1] = env_amount;
    event.resonance = resonance;
    amy_add_event(&event);

    return ESP_OK;
}

esp_err_t amy_esp_adf_set_synth_pan(uint8_t synth, float pan)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.synth = synth;
    event.pan_coefs[COEF_CONST] = pan;
    amy_add_event(&event);

    return ESP_OK;
}

esp_err_t amy_esp_adf_set_fx(float reverb_level, float chorus_level, float echo_level)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    amy_event event = amy_default_event();
    event.reverb_level = reverb_level;
    event.reverb_liveness = REVERB_DEFAULT_LIVENESS;
    event.reverb_damping = REVERB_DEFAULT_DAMPING;
    event.reverb_xover_hz = REVERB_DEFAULT_XOVER_HZ;
    event.chorus_level = chorus_level;
    event.chorus_max_delay = CHORUS_DEFAULT_MAX_DELAY;
    event.chorus_lfo_freq = CHORUS_DEFAULT_LFO_FREQ;
    event.chorus_depth = CHORUS_DEFAULT_MOD_DEPTH;
    event.echo_level = echo_level;
    event.echo_delay_ms = 420.0f;
    event.echo_max_delay_ms = ECHO_DEFAULT_MAX_DELAY_MS;
    event.echo_feedback = 0.22f;
    event.echo_filter_coef = 0.20f;
    amy_add_event(&event);

    return ESP_OK;
}

esp_err_t amy_esp_adf_note_on(uint8_t synth, uint8_t note, float velocity, uint32_t delay_ms)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t target_synth = (synth == 0) ? s_state.default_synth : synth;

    amy_event note_on = amy_default_event();
    note_on.time = amy_sysclock() + delay_ms;
    note_on.synth = target_synth;
    note_on.midi_note = note;
    note_on.velocity = velocity;
    amy_add_event(&note_on);

    return ESP_OK;
}

esp_err_t amy_esp_adf_note_off(uint8_t synth, uint8_t note, uint32_t delay_ms)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t target_synth = (synth == 0) ? s_state.default_synth : synth;

    amy_event note_off = amy_default_event();
    note_off.time = amy_sysclock() + delay_ms;
    note_off.synth = target_synth;
    note_off.midi_note = note;
    note_off.velocity = 0.0f;
    amy_add_event(&note_off);

    return ESP_OK;
}

esp_err_t amy_esp_adf_schedule_note(uint8_t synth, uint8_t note, float velocity, uint16_t duration_ms, uint32_t delay_ms)
{
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = amy_esp_adf_note_on(synth, note, velocity, delay_ms);
    if (err != ESP_OK) {
        return err;
    }

    return amy_esp_adf_note_off(synth, note, delay_ms + duration_ms);
}
