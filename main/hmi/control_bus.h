#ifndef HMI_CONTROL_BUS_H
#define HMI_CONTROL_BUS_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t control_bus_lock_init(void);
esp_err_t control_bus_lock(TickType_t timeout_ticks);
void control_bus_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
