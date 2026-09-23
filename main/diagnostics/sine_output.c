#include "diagnostics/sine_output.h"

#include <string.h>
#include <math.h>
#include <stdint.h>

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SINE_BLOCK_FRAMES 256
#define SINE_CHANNELS 2

static const char *TAG = "DIAG_SINE";
#if !ESP_POCKET_AUDIO_DIAG_SINE_DIGITAL_SILENCE
static const float PI_F = 3.14159265358979323846f;

#if ESP_POCKET_AUDIO_AUTO_SINE_DIAGNOSTIC_MODE && ESP_POCKET_AUDIO_DIAG_SINE_32BIT_I2S
typedef int32_t sine_sample_t;
#define SINE_SAMPLE_MAX 2147483647.0f
#define SINE_I2S_BITS 32

static int32_t clamp_sine_sample(float value)
{
    if (value > 2147483647.0f) {
        return INT32_MAX;
    }
    if (value < -2147483648.0f) {
        return INT32_MIN;
    }
    return (int32_t)value;
}
#else
typedef int16_t sine_sample_t;
#define SINE_SAMPLE_MAX 32767.0f
#define SINE_I2S_BITS 16

static int16_t clamp_sine_sample(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)value;
}
#endif
#else
typedef int16_t sine_sample_t;
#define SINE_I2S_BITS 16
#endif

#if ESP_POCKET_AUDIO_DIAG_SINE_CYCLE_FORMATS
static const char *format_name(bool msb_format)
{
    return msb_format ? "MSB/left-justified" : "Philips/I2S";
}

static esp_err_t configure_i2s_format(const esp_pocket_audio_hw_t *hw, bool msb_format)
{
#if SINE_I2S_BITS == 32
    const i2s_data_bit_width_t width = I2S_DATA_BIT_WIDTH_32BIT;
#else
    const i2s_data_bit_width_t width = I2S_DATA_BIT_WIDTH_16BIT;
#endif
    i2s_std_slot_config_t slot_cfg = msb_format ?
        (i2s_std_slot_config_t)I2S_STD_MSB_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_STEREO) :
        (i2s_std_slot_config_t)I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_STEREO);

    esp_err_t err = i2s_channel_disable(hw->i2s_tx);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(hw->i2s_tx, &slot_cfg), TAG, "I2S slot reconfig failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(hw->i2s_tx), TAG, "I2S re-enable failed");
    ESP_LOGI(TAG, "I2S diagnostic format=%s", format_name(msb_format));
    return ESP_OK;
}
#endif

esp_err_t diagnostics_sine_output_run(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL || hw->i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sine_sample_t samples[SINE_BLOCK_FRAMES * SINE_CHANNELS];
#if !ESP_POCKET_AUDIO_DIAG_SINE_DIGITAL_SILENCE
    const float sample_rate = (float)ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ;
    const float frequency_hz = (float)ESP_POCKET_AUDIO_DIAG_SINE_FREQUENCY_HZ;
    const float amplitude = (float)ESP_POCKET_AUDIO_DIAG_SINE_AMPLITUDE_PERCENT / 100.0f;
    const float phase_step = (2.0f * PI_F * frequency_hz) / sample_rate;
    const float sample_scale = SINE_SAMPLE_MAX * amplitude;
    float phase = 0.0f;
#endif
#if ESP_POCKET_AUDIO_DIAG_SINE_CYCLE_FORMATS
    bool msb_format = false;
    TickType_t next_format_switch = xTaskGetTickCount();
#endif

    ESP_LOGI(TAG,
             "I2S %s output started freq=%uHz amplitude=%u%% sample_rate=%u bits=%u no-AMY",
#if ESP_POCKET_AUDIO_DIAG_SINE_DIGITAL_SILENCE
             "digital-silence",
#elif ESP_POCKET_AUDIO_DIAG_SINE_SQUARE_WAVE
             "square",
#else
             "sine",
#endif
             ESP_POCKET_AUDIO_DIAG_SINE_FREQUENCY_HZ,
             ESP_POCKET_AUDIO_DIAG_SINE_AMPLITUDE_PERCENT,
             ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ,
             SINE_I2S_BITS);

    while (true) {
#if ESP_POCKET_AUDIO_DIAG_SINE_CYCLE_FORMATS
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_format_switch) >= 0) {
            ESP_RETURN_ON_ERROR(configure_i2s_format(hw, msb_format), TAG, "I2S format switch failed");
            msb_format = !msb_format;
            next_format_switch = now + pdMS_TO_TICKS(ESP_POCKET_AUDIO_DIAG_SINE_FORMAT_SECONDS * 1000);
        }
#endif
#if ESP_POCKET_AUDIO_DIAG_SINE_DIGITAL_SILENCE
        memset(samples, 0, sizeof(samples));
#else
        for (size_t frame = 0; frame < SINE_BLOCK_FRAMES; ++frame) {
#if ESP_POCKET_AUDIO_DIAG_SINE_SQUARE_WAVE
            sine_sample_t sample = clamp_sine_sample(phase < PI_F ? sample_scale : -sample_scale);
#else
            sine_sample_t sample = clamp_sine_sample(sinf(phase) * sample_scale);
#endif
            samples[(frame * SINE_CHANNELS) + 0] = sample;
            samples[(frame * SINE_CHANNELS) + 1] = sample;

            phase += phase_step;
            if (phase >= 2.0f * PI_F) {
                phase -= 2.0f * PI_F;
            }
        }
#endif

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(hw->i2s_tx,
                                          samples,
                                          sizeof(samples),
                                          &bytes_written,
                                          portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
            return err;
        }
        if (bytes_written != sizeof(samples)) {
            ESP_LOGW(TAG,
                     "partial I2S write: %u/%u bytes",
                     (unsigned)bytes_written,
                     (unsigned)sizeof(samples));
        }
    }
}
