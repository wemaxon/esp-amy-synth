#ifndef HMI_CONTROL_I2C_H
#define HMI_CONTROL_I2C_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*control_i2c_op_t)(void *ctx);

typedef enum {
    CONTROL_I2C_PRIORITY_LOW = 0,
    CONTROL_I2C_PRIORITY_HIGH,
} control_i2c_priority_t;

esp_err_t control_i2c_service_start(i2c_master_bus_handle_t bus);
i2c_master_bus_handle_t control_i2c_bus(void);
esp_err_t control_i2c_call(control_i2c_op_t op, void *ctx, TickType_t queue_timeout_ticks);
esp_err_t control_i2c_call_priority(
    control_i2c_priority_t priority,
    control_i2c_op_t op,
    void *ctx,
    TickType_t queue_timeout_ticks
);

#ifdef __cplusplus
}
#endif

#endif
