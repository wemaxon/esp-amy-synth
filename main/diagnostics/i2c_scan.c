#include "diagnostics/i2c_scan.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "DIAG_I2C_SCAN";

esp_err_t diagnostics_i2c_scan_bus(
    i2c_master_bus_handle_t bus,
    const char *bus_name,
    uint32_t timeout_ms
)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *name = bus_name != NULL ? bus_name : "I2C";
    bool any_found = false;

    ESP_LOGI(TAG, "scanning %s bus", name);
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        esp_err_t err = i2c_master_probe(bus, addr, timeout_ms);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s device found at 7-bit address 0x%02x", name, addr);
            any_found = true;
        }
    }

    if (!any_found) {
        ESP_LOGW(TAG, "%s scan found no devices", name);
    }

    return ESP_OK;
}
