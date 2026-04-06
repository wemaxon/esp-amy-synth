#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"

#include "board.h"
#include "audio_hal.h"
#include "es8388.h"
#include "amy_esp_adf.h"

static const char *TAG = "main";
static const uint32_t APP_TASK_STACK_SIZE = 12288;

static void log_setup(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);
}

static void dump_es8388_registers(void)
{
    ESP_LOGI(TAG, "=== ES8388 Register Dump ===");
    es8388_read_all();
    ESP_LOGI(TAG, "=== End Register Dump ===");
}

static void configure_codec_outputs(void)
{
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL4, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL5, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL24, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL25, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL26, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL27, 0x1E));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACPOWER, DAC_OUTPUT_ROUT2 | DAC_OUTPUT_LOUT2));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL17, 0x90));
    ESP_ERROR_CHECK(es8388_write_reg(ES8388_DACCONTROL20, 0x90));
}

static void app_task(void *arg)
{
    (void)arg;

    log_setup();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "[1] Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_codec_i2s_iface_t codec_iface = {
        .mode = AUDIO_HAL_MODE_SLAVE,
        .fmt = AUDIO_HAL_I2S_NORMAL,
        .samples = amy_esp_adf_audio_hal_sample_rate(),
        .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
    };
    ESP_ERROR_CHECK(audio_hal_codec_iface_config(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, &codec_iface));
    ESP_ERROR_CHECK(audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START));
    ESP_ERROR_CHECK(audio_hal_set_volume(board_handle->audio_hal, 100));
    ESP_ERROR_CHECK(es8388_pa_power(true));
    ESP_ERROR_CHECK(es8388_set_voice_volume(100));

    configure_codec_outputs();
    dump_es8388_registers();

    ESP_LOGI(TAG, "[2] Start AMY synthesizer");
    amy_esp_adf_config_t amy_cfg = amy_esp_adf_default_config();
    ESP_ERROR_CHECK(amy_esp_adf_init(&amy_cfg));

    ESP_LOGI(TAG, "[3] Create AMY -> I2S pipeline");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    audio_element_handle_t amy_stream = amy_esp_adf_stream_init("amy");

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, amy_stream, "amy");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[2] = {"amy", "i2s"};
    ESP_ERROR_CHECK(audio_pipeline_link(pipeline, link_tag, 2));
    ESP_ERROR_CHECK(i2s_stream_set_clk(i2s_stream_writer, amy_esp_adf_sample_rate(), 16, amy_esp_adf_channels()));
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline));

    ESP_LOGI(TAG, "[4] Pipeline running at %d Hz, 16-bit, %d channels", amy_esp_adf_sample_rate(), amy_esp_adf_channels());

    const uint8_t note_pattern[] = {60, 64, 67, 72};
    size_t note_index = 0;
    while (1) {
        ESP_ERROR_CHECK(amy_esp_adf_schedule_note(0, note_pattern[note_index], 1.0f, 350, 10));
        note_index = (note_index + 1) % (sizeof(note_pattern) / sizeof(note_pattern[0]));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    BaseType_t task_created = xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        APP_TASK_STACK_SIZE,
        NULL,
        5,
        NULL,
        tskNO_AFFINITY
    );

    ESP_ERROR_CHECK((task_created == pdPASS) ? ESP_OK : ESP_FAIL);
}
