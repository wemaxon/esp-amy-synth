#include "hmi/oled_display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include "hmi/control_i2c.h"

void oled_display_draw_rowf(pocket_oled_t *display, int row, const char *fmt, ...)
{
    char line[24];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    pocket_oled_draw_text_row(display, row, line);
}

typedef struct {
    pocket_oled_t *display;
    uint8_t page;
} oled_page_flush_t;

static esp_err_t flush_oled_page(void *ctx)
{
    oled_page_flush_t *flush = (oled_page_flush_t *)ctx;
    if (flush == NULL || flush->display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return pocket_oled_flush_page(flush->display, flush->page);
}

esp_err_t oled_display_flush_cooperatively(pocket_oled_t *display, TickType_t page_timeout_ticks)
{
    if (!pocket_oled_is_initialized(display)) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t page = 0; page < POCKET_OLED_HEIGHT / 8; ++page) {
        oled_page_flush_t flush = {
            .display = display,
            .page = page,
        };
        esp_err_t err = control_i2c_call_priority(
            CONTROL_I2C_PRIORITY_LOW,
            flush_oled_page,
            &flush,
            page_timeout_ticks
        );
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

void oled_display_draw_hline(pocket_oled_t *display, int x, int y, int width, bool on)
{
    for (int i = 0; i < width; ++i) {
        pocket_oled_set_pixel(display, x + i, y, on);
    }
}

void oled_display_draw_vline(pocket_oled_t *display, int x, int y, int height, bool on)
{
    for (int i = 0; i < height; ++i) {
        pocket_oled_set_pixel(display, x, y + i, on);
    }
}

void oled_display_draw_box(pocket_oled_t *display, int x, int y, int width, int height, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    oled_display_draw_hline(display, x, y, width, on);
    oled_display_draw_hline(display, x, y + height - 1, width, on);
    oled_display_draw_vline(display, x, y, height, on);
    oled_display_draw_vline(display, x + width - 1, y, height, on);
}
