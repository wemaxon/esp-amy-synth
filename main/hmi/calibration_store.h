#ifndef HMI_CALIBRATION_STORE_H
#define HMI_CALIBRATION_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*calibration_store_validator_t)(const void *data);

esp_err_t calibration_store_load(const char *nvs_namespace,
                                 const char *key,
                                 void *data,
                                 size_t data_size,
                                 calibration_store_validator_t validator);
esp_err_t calibration_store_save(const char *nvs_namespace,
                                 const char *key,
                                 const void *data,
                                 size_t data_size);
esp_err_t calibration_store_erase(const char *nvs_namespace, const char *key);

#ifdef __cplusplus
}
#endif

#endif

