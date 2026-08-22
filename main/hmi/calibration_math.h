#ifndef HMI_CALIBRATION_MATH_H
#define HMI_CALIBRATION_MATH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float calibration_normalize_centered(int16_t raw,
                                     int16_t minimum_raw,
                                     int16_t center_raw,
                                     int16_t maximum_raw,
                                     int16_t deadzone_raw,
                                     bool invert);
float calibration_normalize_range(int16_t raw,
                                  int16_t minimum_raw,
                                  int16_t maximum_raw,
                                  bool invert);

#ifdef __cplusplus
}
#endif

#endif

