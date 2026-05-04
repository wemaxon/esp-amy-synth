#ifndef ESP_POCKET_AUDIO_AUDIO_BOARD_H
#define ESP_POCKET_AUDIO_AUDIO_BOARD_H

#include "audio_hal.h"
#include "board_def.h"
#include "board_pins_config.h"
#include "display_service.h"
#include "esp_peripherals.h"
#include "esp_pocket_audio_input.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_board_handle {
    audio_hal_handle_t audio_hal;
    audio_hal_handle_t adc_hal;
};

typedef struct audio_board_handle *audio_board_handle_t;

audio_board_handle_t audio_board_init(void);
audio_hal_handle_t audio_board_codec_init(void);
display_service_handle_t audio_board_led_init(void);
esp_err_t audio_board_key_init(esp_periph_set_handle_t set);
audio_board_handle_t audio_board_get_handle(void);
esp_err_t audio_board_deinit(audio_board_handle_t audio_board);

#ifdef __cplusplus
}
#endif

#endif
