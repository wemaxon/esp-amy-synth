#include "hmi/calibration_math.h"

static float clamp_unit(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float clamp_signed_unit(float value)
{
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float calibration_normalize_centered(int16_t raw,
                                     int16_t minimum_raw,
                                     int16_t center_raw,
                                     int16_t maximum_raw,
                                     int16_t deadzone_raw,
                                     bool invert)
{
    int32_t centered = (int32_t)raw - center_raw;
    int32_t deadzone = deadzone_raw;
    float value = 0.0f;

    if (centered >= -deadzone && centered <= deadzone) {
        return 0.0f;
    }
    if (centered > 0) {
        int32_t span = maximum_raw - center_raw - deadzone;
        if (span > 0) {
            value = (float)(centered - deadzone) / (float)span;
        }
    } else {
        int32_t span = center_raw - minimum_raw - deadzone;
        if (span > 0) {
            value = (float)(centered + deadzone) / (float)span;
        }
    }

    value = clamp_signed_unit(value);
    return invert ? -value : value;
}

float calibration_normalize_range(int16_t raw,
                                  int16_t minimum_raw,
                                  int16_t maximum_raw,
                                  bool invert)
{
    int32_t span = (int32_t)maximum_raw - minimum_raw;
    if (span <= 0) {
        return 0.0f;
    }

    float value = ((float)raw - (float)minimum_raw) / (float)span;
    value = clamp_unit(value);
    return invert ? 1.0f - value : value;
}

