#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADS1115_CHANNEL_0 = 0,
    ADS1115_CHANNEL_1 = 1,
    ADS1115_CHANNEL_2 = 2,
    ADS1115_CHANNEL_3 = 3,
} ads1115_channel_t;

typedef struct {
    i2c_port_t i2c_port;
    uint8_t address;
    uint32_t timeout_ms;
} ads1115_t;

esp_err_t ads1115_init(ads1115_t *dev, i2c_port_t i2c_port, uint8_t address, uint32_t timeout_ms);
esp_err_t ads1115_read_single_ended(const ads1115_t *dev, ads1115_channel_t channel, int16_t *raw);

#ifdef __cplusplus
}
#endif

#endif
