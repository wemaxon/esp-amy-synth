#ifndef HMI_JOYSTICK_CALIBRATION_H
#define HMI_JOYSTICK_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JOYSTICK_CAL_AXIS_X = 0,
    JOYSTICK_CAL_AXIS_Y,
    JOYSTICK_CAL_AXIS_COUNT,
} joystick_cal_axis_t;

typedef struct {
    int16_t min_raw;
    int16_t center_raw;
    int16_t max_raw;
    bool invert;
} joystick_cal_axis_config_t;

typedef struct {
    joystick_cal_axis_config_t axis[JOYSTICK_CAL_AXIS_COUNT];
    int16_t deadzone_raw;
    bool saved;
} joystick_calibration_t;

esp_err_t joystick_calibration_init(void);
void joystick_calibration_get(joystick_calibration_t *out);
esp_err_t joystick_calibration_save(void);
esp_err_t joystick_calibration_reset(void);

void joystick_calibration_capture_center(int16_t raw_x, int16_t raw_y);
void joystick_calibration_begin_range_capture(int16_t raw_x, int16_t raw_y);
void joystick_calibration_observe_range(int16_t raw_x, int16_t raw_y);
float joystick_calibration_normalize(joystick_cal_axis_t axis, int16_t raw);

#ifdef __cplusplus
}
#endif

#endif
