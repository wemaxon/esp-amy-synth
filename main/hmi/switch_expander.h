#ifndef HMI_SWITCH_EXPANDER_H
#define HMI_SWITCH_EXPANDER_H

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "pcf8574.h"
#include "pcf8575.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t type;
    uint8_t address;
    uint8_t pin_count;
    union {
        pcf8574_t pcf8574;
        pcf8575_t pcf8575;
    } dev;
} switch_expander_t;

const char *switch_expander_type_name(uint8_t type);
uint8_t switch_expander_type_pin_count(uint8_t type);
esp_err_t switch_expander_init(
    switch_expander_t *expander,
    i2c_master_bus_handle_t bus,
    uint8_t type,
    uint8_t address,
    uint32_t scl_speed_hz,
    uint32_t timeout_ms
);
esp_err_t switch_expander_read_port(switch_expander_t *expander, uint16_t *value);
esp_err_t switch_expander_write_port(switch_expander_t *expander, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
