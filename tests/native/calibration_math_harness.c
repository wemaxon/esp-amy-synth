#include "test_check.h"
#include <stdint.h>

#include "hmi/calibration_math.h"

int32_t host_vector_centered_deadzone(void)
{
    return (int32_t)(calibration_normalize_centered(550, 0, 500, 1000, 100, false) * 1000.0f);
}

int32_t host_vector_centered_positive(void)
{
    return (int32_t)(calibration_normalize_centered(800, 0, 500, 1000, 100, false) * 1000.0f);
}

int32_t host_vector_centered_negative(void)
{
    return (int32_t)(calibration_normalize_centered(200, 0, 500, 1000, 100, false) * 1000.0f);
}

int32_t host_vector_centered_inverted(void)
{
    return (int32_t)(calibration_normalize_centered(800, 0, 500, 1000, 100, true) * 1000.0f);
}

int32_t host_vector_range_quarter(void)
{
    return (int32_t)(calibration_normalize_range(250, 0, 1000, false) * 1000.0f);
}

int32_t host_vector_range_inverted(void)
{
    return (int32_t)(calibration_normalize_range(250, 0, 1000, true) * 1000.0f);
}

int32_t host_vector_range_clamped(void)
{
    return (int32_t)(calibration_normalize_range(2000, 0, 1000, false) * 1000.0f);
}

int32_t host_vector_range_invalid(void)
{
    return (int32_t)(calibration_normalize_range(100, 1000, 1000, false) * 1000.0f);
}

int main(void)
{
    CHECK_VECTOR(host_vector_centered_deadzone, 0);
    CHECK_VECTOR(host_vector_centered_positive, 500);
    CHECK_VECTOR(host_vector_centered_negative, -500);
    CHECK_VECTOR(host_vector_centered_inverted, -500);
    CHECK_VECTOR(host_vector_range_quarter, 250);
    CHECK_VECTOR(host_vector_range_inverted, 750);
    CHECK_VECTOR(host_vector_range_clamped, 1000);
    CHECK_VECTOR(host_vector_range_invalid, 0);
    return 0;
}
