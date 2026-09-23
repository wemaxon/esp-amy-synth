#ifndef POCKET_OLED_H
#define POCKET_OLED_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POCKET_OLED_WIDTH 128
#define POCKET_OLED_HEIGHT 64
#define POCKET_OLED_FRAMEBUFFER_SIZE (POCKET_OLED_WIDTH * POCKET_OLED_HEIGHT / 8)

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t framebuffer[POCKET_OLED_FRAMEBUFFER_SIZE];
    uint32_t timeout_ms;
    bool initialized;
} pocket_oled_t;

esp_err_t pocket_oled_init(
    pocket_oled_t *display,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t scl_speed_hz
);

bool pocket_oled_is_initialized(const pocket_oled_t *display);
void pocket_oled_clear(pocket_oled_t *display);
void pocket_oled_set_pixel(pocket_oled_t *display, int x, int y, bool on);
void pocket_oled_draw_text(pocket_oled_t *display, int x, int y, const char *text);
void pocket_oled_draw_text_scaled(pocket_oled_t *display, int x, int y, const char *text, uint8_t scale);
void pocket_oled_draw_text_row(pocket_oled_t *display, int row, const char *text);
esp_err_t pocket_oled_flush_page(pocket_oled_t *display, uint8_t page);
esp_err_t pocket_oled_flush(pocket_oled_t *display);

#ifdef __cplusplus
}
#endif

#endif
