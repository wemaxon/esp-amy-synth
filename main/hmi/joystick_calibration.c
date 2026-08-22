#include "hmi/joystick_calibration.h"

#include <string.h>

#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "hmi/calibration_math.h"
#include "hmi/calibration_store.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    int16_t deadzone_raw;
    joystick_cal_axis_config_t axis[JOYSTICK_CAL_AXIS_COUNT];
} stored_calibration_t;

static const char *TAG = "JOYSTICK_CAL";
static const char *NVS_NAMESPACE = "joy_cal";
static const char *NVS_KEY = "data";
static const uint32_t CAL_MAGIC = 0x4a43414c;
static const uint16_t CAL_VERSION = 1;

static joystick_calibration_t s_calibration;
static bool s_initialized;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static joystick_calibration_t default_calibration(void)
{
    joystick_calibration_t cal = {
        .axis = {
            [JOYSTICK_CAL_AXIS_X] = {
                .min_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW,
                .center_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW,
                .max_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW,
                .invert = ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_INVERT != 0,
            },
            [JOYSTICK_CAL_AXIS_Y] = {
                .min_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW,
                .center_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW,
                .max_raw = ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW,
                .invert = ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_INVERT != 0,
            },
        },
        .deadzone_raw = ESP_POCKET_AUDIO_ADS1115_DEADZONE_RAW,
        .saved = false,
    };
    return cal;
}

static bool axis_is_valid(const joystick_cal_axis_config_t *axis)
{
    return axis->min_raw < axis->center_raw && axis->center_raw < axis->max_raw;
}

static bool calibration_is_valid(const stored_calibration_t *stored)
{
    return stored->magic == CAL_MAGIC &&
           stored->version == CAL_VERSION &&
           stored->deadzone_raw >= 0 &&
           axis_is_valid(&stored->axis[JOYSTICK_CAL_AXIS_X]) &&
           axis_is_valid(&stored->axis[JOYSTICK_CAL_AXIS_Y]);
}

static bool stored_calibration_is_valid(const void *data)
{
    return calibration_is_valid((const stored_calibration_t *)data);
}

esp_err_t joystick_calibration_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    joystick_calibration_t cal = default_calibration();
    stored_calibration_t stored = {0};
    esp_err_t err = calibration_store_load(NVS_NAMESPACE,
                                           NVS_KEY,
                                           &stored,
                                           sizeof(stored),
                                           stored_calibration_is_valid);
    if (err == ESP_OK) {
        memcpy(cal.axis, stored.axis, sizeof(cal.axis));
        cal.deadzone_raw = stored.deadzone_raw;
        cal.saved = true;
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "ignoring invalid joystick calibration: %s", esp_err_to_name(err));
    }

    cal.axis[JOYSTICK_CAL_AXIS_X].invert =  ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_INVERT != 0;
    cal.axis[JOYSTICK_CAL_AXIS_Y].invert = ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_INVERT != 0;

    portENTER_CRITICAL(&s_lock);
    s_calibration = cal;
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "joystick calibration loaded: source=%s x[%d,%d,%d]%s y[%d,%d,%d]%s dz=%d",
             cal.saved ? "nvs" : "defaults",
             cal.axis[JOYSTICK_CAL_AXIS_X].min_raw,
             cal.axis[JOYSTICK_CAL_AXIS_X].center_raw,
             cal.axis[JOYSTICK_CAL_AXIS_X].max_raw,
             cal.axis[JOYSTICK_CAL_AXIS_X].invert ? " inv" : "",
             cal.axis[JOYSTICK_CAL_AXIS_Y].min_raw,
             cal.axis[JOYSTICK_CAL_AXIS_Y].center_raw,
             cal.axis[JOYSTICK_CAL_AXIS_Y].max_raw,
             cal.axis[JOYSTICK_CAL_AXIS_Y].invert ? " inv" : "",
             cal.deadzone_raw);
    return ESP_OK;
}

void joystick_calibration_get(joystick_calibration_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_calibration;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t joystick_calibration_save(void)
{
    joystick_calibration_t cal = {0};
    joystick_calibration_get(&cal);

    stored_calibration_t stored = {
        .magic = CAL_MAGIC,
        .version = CAL_VERSION,
        .deadzone_raw = cal.deadzone_raw,
    };
    memcpy(stored.axis, cal.axis, sizeof(stored.axis));
    if (!calibration_is_valid(&stored)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = calibration_store_save(NVS_NAMESPACE, NVS_KEY, &stored, sizeof(stored));

    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_calibration.saved = true;
        portEXIT_CRITICAL(&s_lock);
    }
    return err;
}

esp_err_t joystick_calibration_reset(void)
{
    esp_err_t err = calibration_store_erase(NVS_NAMESPACE, NVS_KEY);

    portENTER_CRITICAL(&s_lock);
    s_calibration = default_calibration();
    portEXIT_CRITICAL(&s_lock);
    return err;
}

void joystick_calibration_capture_center(int16_t raw_x, int16_t raw_y)
{
    portENTER_CRITICAL(&s_lock);
    s_calibration.axis[JOYSTICK_CAL_AXIS_X].center_raw = raw_x;
    s_calibration.axis[JOYSTICK_CAL_AXIS_Y].center_raw = raw_y;
    s_calibration.saved = false;
    portEXIT_CRITICAL(&s_lock);
}

void joystick_calibration_begin_range_capture(int16_t raw_x, int16_t raw_y)
{
    portENTER_CRITICAL(&s_lock);
    s_calibration.axis[JOYSTICK_CAL_AXIS_X].min_raw = raw_x;
    s_calibration.axis[JOYSTICK_CAL_AXIS_X].max_raw = raw_x;
    s_calibration.axis[JOYSTICK_CAL_AXIS_Y].min_raw = raw_y;
    s_calibration.axis[JOYSTICK_CAL_AXIS_Y].max_raw = raw_y;
    s_calibration.saved = false;
    portEXIT_CRITICAL(&s_lock);
}

void joystick_calibration_observe_range(int16_t raw_x, int16_t raw_y)
{
    portENTER_CRITICAL(&s_lock);
    joystick_cal_axis_config_t *x = &s_calibration.axis[JOYSTICK_CAL_AXIS_X];
    joystick_cal_axis_config_t *y = &s_calibration.axis[JOYSTICK_CAL_AXIS_Y];
    if (raw_x < x->min_raw) {
        x->min_raw = raw_x;
    }
    if (raw_x > x->max_raw) {
        x->max_raw = raw_x;
    }
    if (raw_y < y->min_raw) {
        y->min_raw = raw_y;
    }
    if (raw_y > y->max_raw) {
        y->max_raw = raw_y;
    }
    s_calibration.saved = false;
    portEXIT_CRITICAL(&s_lock);
}

float joystick_calibration_normalize(joystick_cal_axis_t axis, int16_t raw)
{
    if (axis >= JOYSTICK_CAL_AXIS_COUNT) {
        return 0.0f;
    }

    joystick_calibration_t cal = {0};
    joystick_calibration_get(&cal);
    const joystick_cal_axis_config_t *cfg = &cal.axis[axis];
    return calibration_normalize_centered(raw,
                                          cfg->min_raw,
                                          cfg->center_raw,
                                          cfg->max_raw,
                                          cal.deadzone_raw,
                                          cfg->invert);
}
