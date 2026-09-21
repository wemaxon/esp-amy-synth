#ifndef HOST_TASK_H
#define HOST_TASK_H
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
TickType_t xTaskGetTickCount(void);
#endif
