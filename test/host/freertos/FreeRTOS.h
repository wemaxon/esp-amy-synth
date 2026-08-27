#ifndef HOST_STUB_FREERTOS_H
#define HOST_STUB_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configTICK_RATE_HZ 1000U

#endif
