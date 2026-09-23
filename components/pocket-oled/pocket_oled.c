#include "pocket_oled.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#define SH1106_CONTROL_COMMAND 0x00
#define SH1106_CONTROL_DATA    0x40
#define SH1106_COLUMN_OFFSET   2
#define SH1106_PAGE_COUNT      (POCKET_OLED_HEIGHT / 8)
#define SH1106_TX_BUFFER_SIZE  (POCKET_OLED_WIDTH + 1)
#define SH1106_TIMEOUT_MS      100

typedef struct {
    char ch;
    uint8_t cols[5];
} glyph_t;

static const char *TAG = "POCKET_OLED";

static const glyph_t GLYPHS[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5f, 0x00, 0x00}},
    {'#', {0x14, 0x7f, 0x14, 0x7f, 0x14}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'*', {0x14, 0x08, 0x3e, 0x08, 0x14}},
    {'+', {0x08, 0x08, 0x3e, 0x08, 0x08}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'<', {0x08, 0x14, 0x22, 0x41, 0x00}},
    {'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
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

static esp_err_t sh1106_write_command(pocket_oled_t *display, uint8_t command)
{
    uint8_t tx[2] = {SH1106_CONTROL_COMMAND, command};
    return i2c_master_transmit(display->dev, tx, sizeof(tx), display->timeout_ms);
}

static esp_err_t sh1106_write_commands(pocket_oled_t *display, const uint8_t *commands, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(sh1106_write_command(display, commands[i]), TAG, "SH1106 command 0x%02x failed", commands[i]);
    }
    return ESP_OK;
}

static esp_err_t sh1106_set_page_address(pocket_oled_t *display, uint8_t page)
{
    uint8_t col = SH1106_COLUMN_OFFSET;
    ESP_RETURN_ON_ERROR(sh1106_write_command(display, (uint8_t)(0xB0 | (page & 0x07))), TAG, "set page failed");
    ESP_RETURN_ON_ERROR(sh1106_write_command(display, (uint8_t)(0x00 | (col & 0x0F))), TAG, "set lower column failed");
    ESP_RETURN_ON_ERROR(sh1106_write_command(display, (uint8_t)(0x10 | (col >> 4))), TAG, "set upper column failed");
    return ESP_OK;
}

static esp_err_t sh1106_init_sequence(pocket_oled_t *display)
{
    static const uint8_t init_commands[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0xAD, 0x8B,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0x80,
        0xD9, 0x22,
        0xDB, 0x35,
        0xA4,
        0xA6,
        0xAF,
    };

    return sh1106_write_commands(display, init_commands, sizeof(init_commands));
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
    display->timeout_ms = SH1106_TIMEOUT_MS;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &display->dev), TAG, "SH1106 I2C device init failed");
    ESP_RETURN_ON_ERROR(sh1106_init_sequence(display), TAG, "SH1106 init sequence failed");

    display->initialized = true;
    pocket_oled_clear(display);
    ESP_RETURN_ON_ERROR(pocket_oled_flush(display), TAG, "initial flush failed");

    ESP_LOGI(TAG, "SH1106 OLED configured at 7-bit address 0x%02x", address);
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

void pocket_oled_draw_text_scaled(pocket_oled_t *display, int x, int y, const char *text, uint8_t scale)
{
    if (display == NULL || text == NULL) {
        return;
    }
    if (scale <= 1) {
        pocket_oled_draw_text(display, x, y, text);
        return;
    }

    int cursor_x = x;
    int glyph_width = 5 * (int)scale;
    int advance = 6 * (int)scale;
    while (*text != '\0' && cursor_x <= (POCKET_OLED_WIDTH - glyph_width)) {
        const uint8_t *glyph = glyph_for_char(*text++);
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                bool on = (bits & (1U << row)) != 0;
                for (uint8_t sy = 0; sy < scale; ++sy) {
                    for (uint8_t sx = 0; sx < scale; ++sx) {
                        pocket_oled_set_pixel(display,
                                              cursor_x + (col * (int)scale) + sx,
                                              y + (row * (int)scale) + sy,
                                              on);
                    }
                }
            }
        }
        cursor_x += advance;
    }
}

void pocket_oled_draw_text_row(pocket_oled_t *display, int row, const char *text)
{
    pocket_oled_draw_text(display, 0, row * 8, text);
}

esp_err_t pocket_oled_flush_page(pocket_oled_t *display, uint8_t page)
{
    if (!pocket_oled_is_initialized(display)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (page >= SH1106_PAGE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[SH1106_TX_BUFFER_SIZE] = {0};
    tx[0] = SH1106_CONTROL_DATA;

    ESP_RETURN_ON_ERROR(sh1106_set_page_address(display, page), TAG, "set page %u failed", page);
    memcpy(&tx[1], &display->framebuffer[(size_t)page * POCKET_OLED_WIDTH], POCKET_OLED_WIDTH);
    return i2c_master_transmit(display->dev, tx, sizeof(tx), display->timeout_ms);
}

esp_err_t pocket_oled_flush(pocket_oled_t *display)
{
    if (!pocket_oled_is_initialized(display)) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t page = 0; page < SH1106_PAGE_COUNT; ++page) {
        ESP_RETURN_ON_ERROR(pocket_oled_flush_page(display, page), TAG, "page %u flush failed", page);
    }

    return ESP_OK;
}
