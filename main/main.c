#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "es8388.h"
#include "diagnostics/digital_silence.h"
#include "diagnostics/sine_gate.h"
#include "esp_pocket_audio.h"
#include "esp_pocket_audio_board_config.h"
#include "hmi/controls.h"
#include "hmi/diagnostic_ui.h"
#include "synth/app.h"

static const char *TAG = "main";

static const uint32_t APP_TASK_STACK_SIZE = 8192;
static const BaseType_t APP_TASK_CORE = 0;

static void log_setup(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_POCKET_AUDIO", ESP_LOG_VERBOSE);
    esp_log_level_set("ES8388", ESP_LOG_VERBOSE);
    esp_log_level_set("POCKET_OLED", ESP_LOG_VERBOSE);
    esp_log_level_set("CONTROLS", ESP_LOG_INFO);
    esp_log_level_set("DIAG_SILENCE", ESP_LOG_INFO);
    esp_log_level_set("DIAG_UI", ESP_LOG_VERBOSE);
    esp_log_level_set("JOYSTICK_CAL", ESP_LOG_VERBOSE);
    esp_log_level_set("SYNTH_APP", ESP_LOG_VERBOSE);
    esp_log_level_set("SYNTH_AMY", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void app_task(void *arg)
{
    (void)arg;

    log_setup();
    ESP_LOGI(TAG, "esp-pocket-audio synth app starting");

    init_nvs();
    ESP_ERROR_CHECK(esp_pocket_audio_init());

    const esp_pocket_audio_hw_t *hw = esp_pocket_audio_hw();
    ESP_ERROR_CHECK(hw != NULL ? ESP_OK : ESP_ERR_INVALID_STATE);
    ESP_ERROR_CHECK(es8388_dump_registers((es8388_t *)&hw->codec, TAG));
#if ESP_POCKET_AUDIO_DIAG_SINE_GATE
    ESP_LOGW(TAG, "sine gate diagnostic enabled; synth app will not start");
    ESP_ERROR_CHECK(diagnostics_sine_gate_start(hw));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#elif ESP_POCKET_AUDIO_DIAG_DIGITAL_SILENCE
    ESP_LOGW(TAG, "digital silence diagnostic enabled; synth app will not start");
    ESP_ERROR_CHECK(diagnostics_digital_silence_start(hw));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        ESP_LOGI(TAG, "mode key held on boot, entering diagnostic UI");
        diagnostic_ui_run(hw);
    }
    ESP_ERROR_CHECK(synth_app_start(hw));
    vTaskDelete(NULL);
#endif
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
