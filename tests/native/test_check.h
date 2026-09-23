#ifndef TEST_CHECK_H
#define TEST_CHECK_H
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

/* Deliberately not assert(): checks must execute in Release/NDEBUG too. */
#define CHECK_VECTOR(function, expected) do { \
    int64_t actual_ = (function)(); \
    int64_t expected_ = (expected); \
    if (actual_ != expected_) { \
        fprintf(stderr, "%s: expected %" PRId64 ", got %" PRId64 "\n", \
                #function, expected_, actual_); \
        return 1; \
    } \
} while (0)
#endif
