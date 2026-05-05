#include "pocket_oled.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"

typedef struct {
    char ch;
    uint8_t cols[5];
} glyph_t;

static const char *TAG = "POCKET_OLED";

static const glyph_t GLYPHS[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5f, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}},
    {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4b, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3c, 0x4a, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1e}},
    {'A', {0x7e, 0x11, 0x11, 0x11, 0x7e}},
    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7f, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3e, 0x41, 0x49, 0x49, 0x7a}},
    {'H', {0x7f, 0x08, 0x08, 0x08, 0x7f}},
    {'I', {0x00, 0x41, 0x7f, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3f, 0x01}},
    {'K', {0x7f, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'N', {0x7f, 0x04, 0x08, 0x10, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7f, 0x01, 0x01}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'W', {0x7f, 0x20, 0x18, 0x20, 0x7f}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t UNKNOWN_GLYPH[5] = {0x7f, 0x41, 0x5d, 0x41, 0x7f};

static const uint8_t *glyph_for_char(char ch)
{
    char normalized = (char)toupper((unsigned char)ch);
    for (size_t i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); ++i) {
        if (GLYPHS[i].ch == normalized) {
            return GLYPHS[i].cols;
        }
    }
    return UNKNOWN_GLYPH;
}

esp_err_t pocket_oled_init(
    pocket_oled_t *display,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t scl_speed_hz
)
{
    if (display == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(display, 0, sizeof(*display));

    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address,
        .scl_speed_hz = scl_speed_hz,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_config, &display->io), TAG, "panel IO init failed");

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = POCKET_OLED_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
        .vendor_config = &ssd1306_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(display->io, &panel_config, &display->panel), TAG, "SSD1306 panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG, "panel display enable failed");

    display->initialized = true;
    pocket_oled_clear(display);
    ESP_RETURN_ON_ERROR(pocket_oled_flush(display), TAG, "initial flush failed");

    ESP_LOGI(TAG, "OLED configured at 7-bit address 0x%02x", address);
    return ESP_OK;
}

bool pocket_oled_is_initialized(const pocket_oled_t *display)
{
    return display != NULL && display->initialized;
}

void pocket_oled_clear(pocket_oled_t *display)
{
    if (display == NULL) {
        return;
    }
    memset(display->framebuffer, 0, sizeof(display->framebuffer));
}

void pocket_oled_set_pixel(pocket_oled_t *display, int x, int y, bool on)
{
    if (display == NULL || x < 0 || x >= POCKET_OLED_WIDTH || y < 0 || y >= POCKET_OLED_HEIGHT) {
        return;
    }

    size_t index = (size_t)(y / 8) * POCKET_OLED_WIDTH + (size_t)x;
    uint8_t bit = (uint8_t)(1U << (y & 0x07));
    if (on) {
        display->framebuffer[index] |= bit;
    } else {
        display->framebuffer[index] &= (uint8_t)~bit;
    }
}

void pocket_oled_draw_text(pocket_oled_t *display, int x, int y, const char *text)
{
    if (display == NULL || text == NULL) {
        return;
    }

    int cursor_x = x;
    while (*text != '\0' && cursor_x <= (POCKET_OLED_WIDTH - 5)) {
        const uint8_t *glyph = glyph_for_char(*text++);
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                pocket_oled_set_pixel(display, cursor_x + col, y + row, (bits & (1U << row)) != 0);
            }
        }
        cursor_x += 6;
    }
}

void pocket_oled_draw_text_row(pocket_oled_t *display, int row, const char *text)
{
    pocket_oled_draw_text(display, 0, row * 8, text);
}

esp_err_t pocket_oled_flush(pocket_oled_t *display)
{
    if (!pocket_oled_is_initialized(display)) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_lcd_panel_draw_bitmap(
        display->panel,
        0,
        0,
        POCKET_OLED_WIDTH,
        POCKET_OLED_HEIGHT,
        display->framebuffer
    );
}
