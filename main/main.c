#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "diagnostics/amy_patch_browser.h"
#include "diagnostics/diagnostic_ui.h"
#include "diagnostics/level_calibration.h"
#include "diagnostics/sine_output.h"
#include "esp_pocket_audio.h"
#include "esp_pocket_audio_board_config.h"
#include "hmi/controls.h"
#include "synth/app.h"

static const char *TAG = "main";

static const uint32_t APP_TASK_STACK_SIZE = 8192;
static const BaseType_t APP_TASK_CORE = 0;

static void log_setup(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_POCKET_AUDIO", ESP_LOG_VERBOSE);
    esp_log_level_set("ES8388", ESP_LOG_VERBOSE);
    esp_log_level_set("ES8390", ESP_LOG_VERBOSE);
    esp_log_level_set("POCKET_OLED", ESP_LOG_VERBOSE);
    esp_log_level_set("CONTROLS", ESP_LOG_INFO);
    esp_log_level_set("DIAG_AMY_PATCH", ESP_LOG_INFO);
    esp_log_level_set("DIAG_UI", ESP_LOG_VERBOSE);
    esp_log_level_set("JOYSTICK_CAL", ESP_LOG_VERBOSE);
    esp_log_level_set("SYNTH_APP", ESP_LOG_VERBOSE);
    esp_log_level_set("SYNTH_AMY", ESP_LOG_VERBOSE);
    esp_log_level_set("SYNTH_LOOPER", ESP_LOG_VERBOSE);
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

static void discard_pending_control_events(void)
{
    control_event_t event;
    while (controls_pop_event(&event) == ESP_OK) {
    }
}

#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
static bool run_level_calibration(const esp_pocket_audio_hw_t *hw)
{
    esp_err_t err = diagnostics_level_calibration_run(hw);
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG,
             "level calibration stopped: %s (0x%x)",
             esp_err_to_name(err),
             (unsigned)err);
    return false;
}
#endif

static void app_task(void *arg)
{
    (void)arg;

    log_setup();
    ESP_LOGI(TAG, "esp-pocket-audio synth app starting");

    init_nvs();
    ESP_ERROR_CHECK(esp_pocket_audio_init());

    const esp_pocket_audio_hw_t *hw = esp_pocket_audio_hw();
    ESP_ERROR_CHECK(hw != NULL ? ESP_OK : ESP_ERR_INVALID_STATE);
    bool level_calibration_ran = false;
#if ESP_POCKET_AUDIO_AUTO_SINE_DIAGNOSTIC_MODE
    ESP_LOGI(TAG, "auto sine diagnostic enabled, starting direct I2S sine output");
    ESP_ERROR_CHECK(diagnostics_sine_output_run(hw));
#endif
#if ESP_POCKET_AUDIO_AUTO_DIAGNOSTIC_MODE
    ESP_LOGI(TAG, "auto diagnostic mode enabled, entering diagnostic UI");
    diagnostic_ui_run(hw);
#endif
#if ESP_POCKET_AUDIO_AUTO_LEVEL_CALIBRATION_MODE
    ESP_LOGI(TAG, "auto level calibration enabled, starting fixture sweep");
    if (!run_level_calibration(hw)) {
        vTaskDelete(NULL);
        return;
    }
    level_calibration_ran = true;
#endif
#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU) &&
        controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        ESP_LOGI(TAG, "sound+mode held on boot, starting level calibration");
        if (!run_level_calibration(hw)) {
            vTaskDelete(NULL);
            return;
        }
        level_calibration_ran = true;
    }
#endif
    if (level_calibration_ran) {
        discard_pending_control_events();
    }
    if (!level_calibration_ran && controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        ESP_LOGI(TAG, "sound key held on boot, entering AMY patch browser");
        ESP_ERROR_CHECK(diagnostics_amy_patch_browser_run(hw));
    }
    if (!level_calibration_ran && controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        ESP_LOGI(TAG, "mode key held on boot, entering diagnostic UI");
        diagnostic_ui_run(hw);
    }
    ESP_ERROR_CHECK(synth_app_start(hw));
    vTaskDelete(NULL);
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
