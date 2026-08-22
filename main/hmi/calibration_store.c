#include "hmi/calibration_store.h"

#include "nvs.h"

static bool arguments_valid(const char *nvs_namespace,
                            const char *key,
                            const void *data,
                            size_t data_size)
{
    return nvs_namespace != NULL && key != NULL && data != NULL && data_size > 0;
}

esp_err_t calibration_store_load(const char *nvs_namespace,
                                 const char *key,
                                 void *data,
                                 size_t data_size,
                                 calibration_store_validator_t validator)
{
    if (!arguments_valid(nvs_namespace, key, data, data_size)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t stored_size = 0;
    err = nvs_get_blob(handle, key, NULL, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    if (stored_size != data_size) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_get_blob(handle, key, data, &stored_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (validator != NULL && !validator(data)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t calibration_store_save(const char *nvs_namespace,
                                 const char *key,
                                 const void *data,
                                 size_t data_size)
{
    if (!arguments_valid(nvs_namespace, key, data, data_size)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, key, data, data_size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t calibration_store_erase(const char *nvs_namespace, const char *key)
{
    if (nvs_namespace == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
