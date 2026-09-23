#ifndef PCF8574_H
#define PCF8574_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t output_latch;
    uint32_t timeout_ms;
} pcf8574_t;

esp_err_t pcf8574_init(
    pcf8574_t *dev,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t scl_speed_hz,
    uint32_t timeout_ms
);
esp_err_t pcf8574_read_port(const pcf8574_t *dev, uint8_t *value);
esp_err_t pcf8574_write_port(pcf8574_t *dev, uint8_t value);
esp_err_t pcf8574_read_pin(const pcf8574_t *dev, uint8_t pin, bool *level);
esp_err_t pcf8574_write_pin(pcf8574_t *dev, uint8_t pin, bool level);

#ifdef __cplusplus
}
#endif

#endif
