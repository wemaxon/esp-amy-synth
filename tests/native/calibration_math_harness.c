#include "test_check.h"
#include <math.h>
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

/* Circular dead band: zero inside, rescaled outside, direction preserved. */
int32_t host_vector_radial_deadzone(void)
{
    float x = 0.07f, y = -0.07f;
    calibration_apply_radial_deadzone(&x, &y, 0.1f);
    if (x != 0.0f || y != 0.0f) return 0;
    /* r = 0.5 -> (0.5 - 0.1) / 0.9 = 0.444, same 3:4 direction */
    x = 0.3f; y = 0.4f;
    calibration_apply_radial_deadzone(&x, &y, 0.1f);
    if (fabsf(x - 0.2667f) > 0.001f || fabsf(y - 0.3556f) > 0.001f) return 0;
    /* full travel stays full travel */
    x = -0.6f; y = 0.8f;
    calibration_apply_radial_deadzone(&x, &y, 0.1f);
    if (fabsf(x + 0.6f) > 0.001f || fabsf(y - 0.8f) > 0.001f) return 0;
    /* a diagonal just outside the band is not snapped to an axis */
    x = 0.08f; y = 0.08f;
    calibration_apply_radial_deadzone(&x, &y, 0.1f);
    return x > 0.0f && fabsf(x - y) < 1e-6f;
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
    CHECK_VECTOR(host_vector_radial_deadzone, 1);
    return 0;
}
