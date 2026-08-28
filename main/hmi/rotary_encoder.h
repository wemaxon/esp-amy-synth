#ifndef HMI_ROTARY_ENCODER_H
#define HMI_ROTARY_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "hmi/rotary_encoder_position.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t phase_a_gpio;
    gpio_num_t phase_b_gpio;
    uint8_t counts_per_detent;
    int8_t direction;
    uint32_t glitch_filter_ns;
    bool internal_pullups;
} rotary_encoder_config_t;

typedef struct {
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t phase_a_channel;
    pcnt_channel_handle_t phase_b_channel;
    uint8_t counts_per_detent;
    int8_t direction;
    int last_count;
    rotary_encoder_position_t position;
    bool initialized;
} rotary_encoder_t;

esp_err_t rotary_encoder_init(rotary_encoder_t *encoder,
                              const rotary_encoder_config_t *config);
esp_err_t rotary_encoder_get_position(rotary_encoder_t *encoder,
                                      int32_t *position);

#ifdef __cplusplus
}
#endif

#endif
