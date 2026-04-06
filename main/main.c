#include "esp_check.h"
#include "esp_log.h"


#include "es8388.h"


static const char *TAG = "main";


void app_main(void)
{
    ESP_LOGI(TAG, "Running main");

    audio_hal_codec_config_t codec_cfg = {
        .adc_input = AUDIO_HAL_ADC_INPUT_LINE1,
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
        .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,
        .i2s_iface = {
            .mode = AUDIO_HAL_MODE_SLAVE,
            .fmt = AUDIO_HAL_I2S_NORMAL,
            .samples = AUDIO_HAL_16K_SAMPLES,
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
        },
    };

    ESP_ERROR_CHECK(es8388_init(&codec_cfg));
    ESP_ERROR_CHECK(es8388_start(ES_MODULE_ADC_DAC));
}
