#ifndef HMI_OLED_DISPLAY_H
#define HMI_OLED_DISPLAY_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "pocket_oled.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_display_flush_cooperatively(pocket_oled_t *display, TickType_t page_timeout_ticks);
void oled_display_draw_hline(pocket_oled_t *display, int x, int y, int width, bool on);
void oled_display_draw_vline(pocket_oled_t *display, int x, int y, int height, bool on);
void oled_display_draw_box(pocket_oled_t *display, int x, int y, int width, int height, bool on);

#ifdef __cplusplus
}
#endif

#endif
