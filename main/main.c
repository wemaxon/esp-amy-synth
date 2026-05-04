#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "esp_peripherals.h"

#include "board.h"
#include "audio_hal.h"
#include "es8388.h"
#include "periph_button.h"
#include "app_beat.h"
#include "app_synth.h"
#if defined(CONFIG_AUDIO_BOARD_ESP_POCKET_AUDIO)
#include "esp_pocket_audio_input.h"
#endif

static const char *TAG = "main";
static const uint32_t APP_TASK_STACK_SIZE = 12288;
static const BaseType_t APP_TASK_CORE = 0;
static const BaseType_t AUDIO_TASK_CORE = 1;
static const float BEAT_DEFAULT_BPM = 120.0f;
static const uint16_t BEAT_DEFAULT_PPQN = 24;
static const uint8_t BEAT_DEFAULT_BEATS_PER_BAR = 4;

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

#if defined(CONFIG_AUDIO_BOARD_ESP_POCKET_AUDIO)
static void handle_esp_pocket_audio_input(void)
{
    esp_err_t err = esp_pocket_audio_input_update();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Input update failed: %s", esp_err_to_name(err));
        return;
    }

    esp_pocket_audio_input_event_t event = {0};
    while (esp_pocket_audio_input_pop_event(&event) == ESP_OK) {
        audio_event_iface_msg_t msg = {
            .source_type = PERIPH_ID_BUTTON,
            .source = NULL,
            .cmd = event.type == ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_PRESSED ?
                PERIPH_BUTTON_PRESSED :
                PERIPH_BUTTON_RELEASE,
            .data = (void *)(intptr_t)event.sw,
            .data_len = 0,
            .need_free_data = false,
        };
        (void)app_synth_handle_button_event(&msg);
    }
}
#endif

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
        .samples = AUDIO_HAL_44K_SAMPLES,
        .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
    };
    ESP_ERROR_CHECK(audio_hal_codec_iface_config(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, &codec_iface));
    ESP_ERROR_CHECK(audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START));
    ESP_ERROR_CHECK(audio_hal_set_volume(board_handle->audio_hal, 100));
    ESP_ERROR_CHECK(es8388_pa_power(true));
    ESP_ERROR_CHECK(es8388_set_voice_volume(100));

    configure_codec_outputs();
    dump_es8388_registers();

    ESP_ERROR_CHECK(app_beat_init(BEAT_DEFAULT_BPM, BEAT_DEFAULT_PPQN, BEAT_DEFAULT_BEATS_PER_BAR));

    ESP_LOGI(TAG, "[2] Start AMY synthesizer");
    ESP_ERROR_CHECK(app_synth_init());

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_cfg.task_core = APP_TASK_CORE;
    esp_periph_set_handle_t periph_set = esp_periph_set_init(&periph_cfg);
    ESP_ERROR_CHECK(audio_board_key_init(periph_set));

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t button_evt = audio_event_iface_init(&evt_cfg);
    ESP_ERROR_CHECK(audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set), button_evt));

    ESP_LOGI(TAG, "[3] Create AMY -> I2S pipeline");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    audio_element_handle_t amy_stream = app_synth_create_stream(AUDIO_TASK_CORE, 22);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_core = AUDIO_TASK_CORE;
    i2s_cfg.task_prio = 23;
    audio_element_handle_t i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, amy_stream, "amy");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[2] = {"amy", "i2s"};
    ESP_ERROR_CHECK(audio_pipeline_link(pipeline, link_tag, 2));
    ESP_ERROR_CHECK(i2s_stream_set_clk(i2s_stream_writer, app_synth_sample_rate(), 16, app_synth_channels()));
    ESP_ERROR_CHECK(audio_pipeline_run(pipeline));

    ESP_LOGI(TAG, "[4] Pipeline running at %d Hz, 16-bit, %d channels", app_synth_sample_rate(), app_synth_channels());

    while (1) {
        TickType_t now_ticks = xTaskGetTickCount();
        ESP_ERROR_CHECK(app_beat_update(now_ticks));
        app_synth_update((uint32_t)now_ticks);

#if defined(CONFIG_AUDIO_BOARD_ESP_POCKET_AUDIO)
        handle_esp_pocket_audio_input();
#endif

        audio_event_iface_msg_t msg = {0};
        esp_err_t ret = audio_event_iface_listen(button_evt, &msg, pdMS_TO_TICKS(20));
        if (ret == ESP_OK) {
            (void)app_synth_handle_button_event(&msg);
        }
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
        APP_TASK_CORE
    );

    ESP_ERROR_CHECK((task_created == pdPASS) ? ESP_OK : ESP_FAIL);
}
