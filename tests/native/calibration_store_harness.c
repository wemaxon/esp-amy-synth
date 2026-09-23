#include "test_check.h"
#include <stdbool.h>
#include <stdint.h>

#include "nvs.h"

enum {
    SCENARIO_VALID,
    SCENARIO_NAMESPACE_MISSING,
    SCENARIO_KEY_MISSING,
    SCENARIO_SHORT_BLOB,
    SCENARIO_INVALID_BLOB,
    SCENARIO_ERASE_MISSING,
};

static int s_scenario;
static uint32_t s_commits;

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    (void)name;
    (void)open_mode;
    if (s_scenario == SCENARIO_NAMESPACE_MISSING) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out_handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    (void)key;
    if (s_scenario == SCENARIO_KEY_MISSING) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (s_scenario == SCENARIO_SHORT_BLOB) {
        *length = 2;
        return ESP_OK;
    }

    *length = sizeof(uint32_t);
    if (out_value != NULL) {
        *(uint32_t *)out_value = s_scenario == SCENARIO_INVALID_BLOB ? 0U : 42U;
    }
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    return s_scenario == SCENARIO_ERASE_MISSING ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    ++s_commits;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

#include "hmi/calibration_store.h"

static bool value_is_valid(const void *data)
{
    return *(const uint32_t *)data == 42U;
}

int32_t host_vector_store_load_valid(void)
{
    s_scenario = SCENARIO_VALID;
    uint32_t data = 0;
    esp_err_t err = calibration_store_load("cal", "data", &data, sizeof(data), value_is_valid);
    return err == ESP_OK ? (int32_t)data : err;
}

int32_t host_vector_store_namespace_missing(void)
{
    s_scenario = SCENARIO_NAMESPACE_MISSING;
    uint32_t data = 0;
    return calibration_store_load("cal", "data", &data, sizeof(data), value_is_valid);
}

int32_t host_vector_store_key_missing(void)
{
    s_scenario = SCENARIO_KEY_MISSING;
    uint32_t data = 0;
    return calibration_store_load("cal", "data", &data, sizeof(data), value_is_valid);
}

int32_t host_vector_store_wrong_size(void)
{
    s_scenario = SCENARIO_SHORT_BLOB;
    uint32_t data = 0;
    return calibration_store_load("cal", "data", &data, sizeof(data), value_is_valid);
}

int32_t host_vector_store_invalid_blob(void)
{
    s_scenario = SCENARIO_INVALID_BLOB;
    uint32_t data = 0;
    return calibration_store_load("cal", "data", &data, sizeof(data), value_is_valid);
}

int32_t host_vector_store_erase_missing(void)
{
    s_scenario = SCENARIO_ERASE_MISSING;
    s_commits = 0;
    esp_err_t err = calibration_store_erase("cal", "data");
    return err == ESP_OK ? (int32_t)s_commits : err;
}

int main(void)
{
    CHECK_VECTOR(host_vector_store_load_valid, 42);
    CHECK_VECTOR(host_vector_store_namespace_missing, 261);
    CHECK_VECTOR(host_vector_store_key_missing, 261);
    CHECK_VECTOR(host_vector_store_wrong_size, 260);
    CHECK_VECTOR(host_vector_store_invalid_blob, 259);
    CHECK_VECTOR(host_vector_store_erase_missing, 1);
    return 0;
}
