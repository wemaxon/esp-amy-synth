#include "hmi/wheel_calibration.h"

#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    int16_t min_raw;
    int16_t max_raw;
    uint8_t invert;
} stored_calibration_t;

static const char *TAG = "WHEEL_CAL";
static const char *NVS_NAMESPACE = "wheel_cal";
static const char *NVS_KEY = "data";
static const uint32_t CAL_MAGIC = 0x5743414c;
static const uint16_t CAL_VERSION = 1;

static wheel_calibration_t s_calibration;
static bool s_initialized;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static wheel_calibration_t default_calibration(void)
{
    wheel_calibration_t cal = {
        .min_raw = ESP_POCKET_AUDIO_WHEEL_MIN_RAW,
        .max_raw = ESP_POCKET_AUDIO_WHEEL_MAX_RAW,
        .invert = ESP_POCKET_AUDIO_WHEEL_INVERT != 0,
        .saved = false,
    };
    return cal;
}

static bool calibration_is_valid(const stored_calibration_t *stored)
{
    return stored->magic == CAL_MAGIC &&
           stored->version == CAL_VERSION &&
           stored->min_raw < stored->max_raw &&
           stored->invert <= 1;
}

esp_err_t wheel_calibration_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    wheel_calibration_t cal = default_calibration();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        stored_calibration_t stored = {0};
        size_t size = sizeof(stored);
        err = nvs_get_blob(handle, NVS_KEY, &stored, &size);
        nvs_close(handle);
        if (err == ESP_OK && size == sizeof(stored) && calibration_is_valid(&stored)) {
            cal.min_raw = stored.min_raw;
            cal.max_raw = stored.max_raw;
            cal.invert = stored.invert != 0;
            cal.saved = true;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "ignoring invalid wheel calibration: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS open failed for wheel calibration: %s", esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_lock);
    s_calibration = cal;
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG,
             "wheel calibration loaded: source=%s range[%d,%d]%s",
             cal.saved ? "nvs" : "defaults",
             cal.min_raw,
             cal.max_raw,
             cal.invert ? " inv" : "");
    return ESP_OK;
}

void wheel_calibration_get(wheel_calibration_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_calibration;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t wheel_calibration_save(void)
{
    wheel_calibration_t cal = {0};
    wheel_calibration_get(&cal);

    stored_calibration_t stored = {
        .magic = CAL_MAGIC,
        .version = CAL_VERSION,
        .min_raw = cal.min_raw,
        .max_raw = cal.max_raw,
        .invert = cal.invert ? 1 : 0,
    };
    if (!calibration_is_valid(&stored)) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_calibration.saved = true;
        portEXIT_CRITICAL(&s_lock);
    }
    return err;
}

esp_err_t wheel_calibration_reset(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        esp_err_t erase_err = nvs_erase_key(handle, NVS_KEY);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            err = erase_err;
        } else {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    portENTER_CRITICAL(&s_lock);
    s_calibration = default_calibration();
    portEXIT_CRITICAL(&s_lock);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

void wheel_calibration_begin_range_capture(int16_t raw)
{
    portENTER_CRITICAL(&s_lock);
    s_calibration.min_raw = raw;
    s_calibration.max_raw = raw;
    s_calibration.saved = false;
    portEXIT_CRITICAL(&s_lock);
}

void wheel_calibration_observe_range(int16_t raw)
{
    portENTER_CRITICAL(&s_lock);
    if (raw < s_calibration.min_raw) {
        s_calibration.min_raw = raw;
    }
    if (raw > s_calibration.max_raw) {
        s_calibration.max_raw = raw;
    }
    s_calibration.saved = false;
    portEXIT_CRITICAL(&s_lock);
}

float wheel_calibration_normalize(int16_t raw)
{
    wheel_calibration_t cal = {0};
    wheel_calibration_get(&cal);

    int32_t span = (int32_t)cal.max_raw - cal.min_raw;
    if (span <= 0) {
        return 0.0f;
    }

    float value = ((float)raw - (float)cal.min_raw) / (float)span;
    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > 1.0f) {
        value = 1.0f;
    }

    return cal.invert ? 1.0f - value : value;
}
