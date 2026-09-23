#ifndef HMI_WHEEL_CALIBRATION_H
#define HMI_WHEEL_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t min_raw;
    int16_t max_raw;
    bool invert;
    bool saved;
} wheel_calibration_t;

esp_err_t wheel_calibration_init(void);
void wheel_calibration_get(wheel_calibration_t *out);
esp_err_t wheel_calibration_save(void);
esp_err_t wheel_calibration_reset(void);

void wheel_calibration_begin_range_capture(int16_t raw);
void wheel_calibration_observe_range(int16_t raw);
float wheel_calibration_normalize(int16_t raw);

#ifdef __cplusplus
}
#endif

#endif
