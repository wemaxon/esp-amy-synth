#include "test_check.h"
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"

static uint8_t s_tx[2];
static size_t s_tx_count;
static uint8_t s_rx[2];
static esp_err_t s_transmit_result;

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device)
{
    (void)bus;
    (void)config;
    *device = (i2c_master_dev_handle_t)(uintptr_t)1;
    return ESP_OK;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
                             uint8_t *bytes,
                             size_t byte_count,
                             uint32_t timeout_ms)
{
    (void)device;
    (void)timeout_ms;
    for (size_t i = 0; i < byte_count; ++i) {
        bytes[i] = s_rx[i];
    }
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *bytes,
                              size_t byte_count,
                              uint32_t timeout_ms)
{
    (void)device;
    (void)timeout_ms;
    s_tx_count = byte_count;
    for (size_t i = 0; i < byte_count; ++i) {
        s_tx[i] = bytes[i];
    }
    return s_transmit_result;
}

#include "pcf857x.h"
#include "pcf8574.h"
#include "pcf8575.h"

static void reset_fake_bus(void)
{
    s_tx[0] = 0;
    s_tx[1] = 0;
    s_tx_count = 0;
    s_rx[0] = 0;
    s_rx[1] = 0;
    s_transmit_result = ESP_OK;
}

int32_t host_vector_pcf8574_init_latch(void)
{
    pcf8574_t device;
    reset_fake_bus();
    pcf8574_init(&device, (i2c_master_bus_handle_t)(uintptr_t)1, 0x20, 400000, 10);
    return device.output_latch;
}

int32_t host_vector_pcf8575_init_latch(void)
{
    pcf8575_t device;
    reset_fake_bus();
    pcf8575_init(&device, (i2c_master_bus_handle_t)(uintptr_t)1, 0x20, 400000, 10);
    return device.output_latch;
}

int32_t host_vector_pcf8575_write_encoding(void)
{
    pcf8575_t device;
    reset_fake_bus();
    pcf8575_init(&device, (i2c_master_bus_handle_t)(uintptr_t)1, 0x20, 400000, 10);
    pcf8575_write_port(&device, 0xabcd);
    return ((int32_t)s_tx_count << 16) | ((int32_t)s_tx[1] << 8) | s_tx[0];
}

int32_t host_vector_pcf8575_read_decoding(void)
{
    pcf8575_t device;
    uint16_t value = 0;
    reset_fake_bus();
    pcf8575_init(&device, (i2c_master_bus_handle_t)(uintptr_t)1, 0x20, 400000, 10);
    s_rx[0] = 0x34;
    s_rx[1] = 0x12;
    pcf8575_read_port(&device, &value);
    return value;
}

int32_t host_vector_pcf8575_failed_write_latch(void)
{
    pcf8575_t device;
    reset_fake_bus();
    pcf8575_init(&device, (i2c_master_bus_handle_t)(uintptr_t)1, 0x20, 400000, 10);
    s_transmit_result = ESP_FAIL;
    pcf8575_write_port(&device, 0);
    return device.output_latch;
}

int32_t host_vector_pcf8574_pin_bound(void)
{
    pcf8574_t device = {0};
    return pcf8574_write_pin(&device, 8, true);
}

int main(void)
{
    CHECK_VECTOR(host_vector_pcf8574_init_latch, 255);
    CHECK_VECTOR(host_vector_pcf8575_init_latch, 65535);
    CHECK_VECTOR(host_vector_pcf8575_write_encoding, 175053);
    CHECK_VECTOR(host_vector_pcf8575_read_decoding, 4660);
    CHECK_VECTOR(host_vector_pcf8575_failed_write_latch, 65535);
    CHECK_VECTOR(host_vector_pcf8574_pin_bound, 258);
    return 0;
}
