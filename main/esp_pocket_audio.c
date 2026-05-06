#include "esp_pocket_audio.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "hmi/control_bus.h"
#include "hmi/controls.h"
#include "pocket_oled.h"

static const char *TAG = "ESP_POCKET_AUDIO";

static esp_pocket_audio_hw_t s_hw;
static bool s_initialized;

static uint64_t gpio_mask(gpio_num_t gpio)
{
    return gpio == GPIO_NUM_NC ? 0 : 1ULL << gpio;
}

static void scan_codec_i2c_bus(void)
{
    bool any_found = false;

    ESP_LOGW(TAG, "scanning codec I2C bus");
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        esp_err_t err = i2c_master_probe(
            s_hw.codec_i2c_bus,
            addr,
            ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS
        );
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "codec I2C device found at 7-bit address 0x%02x", addr);
            any_found = true;
        }
    }

    if (!any_found) {
        ESP_LOGW(TAG, "codec I2C scan found no devices");
    }
}

static esp_err_t init_codec_i2c(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = ESP_POCKET_AUDIO_CODEC_I2C_PORT,
        .sda_io_num = ESP_POCKET_AUDIO_CODEC_I2C_SDA_GPIO,
        .scl_io_num = ESP_POCKET_AUDIO_CODEC_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_LOGI(TAG, "codec I2C: port=%d SDA=%d SCL=%d",
             ESP_POCKET_AUDIO_CODEC_I2C_PORT,
             ESP_POCKET_AUDIO_CODEC_I2C_SDA_GPIO,
             ESP_POCKET_AUDIO_CODEC_I2C_SCL_GPIO);
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_hw.codec_i2c_bus), TAG, "codec I2C bus init failed");

    esp_err_t probe = i2c_master_probe(
        s_hw.codec_i2c_bus,
        ESP_POCKET_AUDIO_ES8388_ADDR,
        ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS
    );
    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 not detected at 7-bit address 0x%02x", ESP_POCKET_AUDIO_ES8388_ADDR);
        scan_codec_i2c_bus();
        return probe;
    }

    ESP_LOGI(TAG, "ES8388 acknowledged at 7-bit address 0x%02x", ESP_POCKET_AUDIO_ES8388_ADDR);
    return ESP_OK;
}

static esp_err_t init_control_i2c(void)
{
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = ESP_POCKET_AUDIO_CONTROL_I2C_PORT,
        .sda_io_num = ESP_POCKET_AUDIO_CONTROL_I2C_SDA_GPIO,
        .scl_io_num = ESP_POCKET_AUDIO_CONTROL_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_LOGI(TAG, "control I2C: port=%d SDA=%d SCL=%d",
             ESP_POCKET_AUDIO_CONTROL_I2C_PORT,
             ESP_POCKET_AUDIO_CONTROL_I2C_SDA_GPIO,
             ESP_POCKET_AUDIO_CONTROL_I2C_SCL_GPIO);

    return i2c_new_master_bus(&i2c_cfg, &s_hw.control_i2c_bus);
}

static esp_err_t init_display(void)
{
#if ESP_POCKET_AUDIO_OLED_ENABLED
    esp_err_t err = control_bus_lock(pdMS_TO_TICKS(500));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED could not lock control I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(
        s_hw.control_i2c_bus,
        ESP_POCKET_AUDIO_OLED_ADDR,
        ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        control_bus_unlock();
        ESP_LOGE(TAG, "OLED not detected at configured 7-bit address 0x%02x", ESP_POCKET_AUDIO_OLED_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    err = pocket_oled_init(
        &s_hw.display,
        s_hw.control_i2c_bus,
        ESP_POCKET_AUDIO_OLED_ADDR,
        ESP_POCKET_AUDIO_OLED_I2C_CLK_HZ
    );
    if (err == ESP_OK) {
        pocket_oled_draw_text_row(&s_hw.display, 0, "ESP-POCKET AUDIO");
        pocket_oled_draw_text_row(&s_hw.display, 2, "HARDWARE READY");
        pocket_oled_draw_text_row(&s_hw.display, 4, "CONTROLS READY");
        err = pocket_oled_flush(&s_hw.display);
    }
    control_bus_unlock();
    ESP_RETURN_ON_ERROR(err, TAG, "OLED init failed");
#else
    ESP_LOGI(TAG, "OLED display is disabled");
#endif
    return ESP_OK;
}

static esp_err_t init_i2s_clocking(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ESP_POCKET_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_hw.i2s_tx, NULL), TAG, "I2S channel init failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ESP_POCKET_AUDIO_I2S_MCLK_GPIO,
            .bclk = ESP_POCKET_AUDIO_I2S_BCLK_GPIO,
            .ws = ESP_POCKET_AUDIO_I2S_WS_GPIO,
            .dout = ESP_POCKET_AUDIO_I2S_DOUT_GPIO,
            .din = ESP_POCKET_AUDIO_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_LOGI(TAG, "I2S: port=%d mclk=%d bclk=%d ws=%d dout=%d din=%d rate=%d",
             ESP_POCKET_AUDIO_I2S_PORT,
             ESP_POCKET_AUDIO_I2S_MCLK_GPIO,
             ESP_POCKET_AUDIO_I2S_BCLK_GPIO,
             ESP_POCKET_AUDIO_I2S_WS_GPIO,
             ESP_POCKET_AUDIO_I2S_DOUT_GPIO,
             ESP_POCKET_AUDIO_I2S_DIN_GPIO,
             ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ);

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_hw.i2s_tx, &std_cfg), TAG, "I2S std mode init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_hw.i2s_tx), TAG, "I2S enable failed");
    return ESP_OK;
}

static esp_err_t init_pa_gpio(void)
{
    if (ESP_POCKET_AUDIO_PA_ENABLE_GPIO == GPIO_NUM_NC) {
        ESP_LOGI(TAG, "PA enable GPIO is not configured");
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = gpio_mask(ESP_POCKET_AUDIO_PA_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "PA GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(ESP_POCKET_AUDIO_PA_ENABLE_GPIO, 1), TAG, "PA GPIO enable failed");
    ESP_LOGI(TAG, "PA enabled on GPIO%d", ESP_POCKET_AUDIO_PA_ENABLE_GPIO);
    return ESP_OK;
}

esp_err_t esp_pocket_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_codec_i2c(), TAG, "codec I2C failed");
    ESP_RETURN_ON_ERROR(es8388_init(
        &s_hw.codec,
        s_hw.codec_i2c_bus,
        ESP_POCKET_AUDIO_ES8388_ADDR,
        ESP_POCKET_AUDIO_CODEC_I2C_CLK_HZ,
        ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS
    ), TAG, "ES8388 init failed");
    ESP_RETURN_ON_ERROR(init_i2s_clocking(), TAG, "I2S failed");
    ESP_RETURN_ON_ERROR(es8388_start(&s_hw.codec), TAG, "ES8388 start failed");
    ESP_RETURN_ON_ERROR(es8388_set_volume(&s_hw.codec, 100), TAG, "ES8388 volume failed");
    ESP_RETURN_ON_ERROR(init_pa_gpio(), TAG, "PA GPIO failed");
    ESP_RETURN_ON_ERROR(init_control_i2c(), TAG, "control I2C failed");
    ESP_RETURN_ON_ERROR(control_bus_lock_init(), TAG, "control bus lock init failed");
    ESP_RETURN_ON_ERROR(controls_init(s_hw.control_i2c_bus), TAG, "controls init failed");
    ESP_RETURN_ON_ERROR(init_display(), TAG, "display init failed");

    s_initialized = true;
    ESP_LOGI(TAG, "hardware init complete");
    return ESP_OK;
}

const esp_pocket_audio_hw_t *esp_pocket_audio_hw(void)
{
    return s_initialized ? &s_hw : NULL;
}
