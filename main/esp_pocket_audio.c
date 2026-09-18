#include "esp_pocket_audio.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "diagnostics/i2c_scan.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/control_i2c.h"
#include "hmi/controls.h"
#include "pocket_oled.h"

static const char *TAG = "ESP_POCKET_AUDIO";

static esp_pocket_audio_hw_t s_hw;
static bool s_initialized;
static bool s_codec_address_straps_initialized;
#if ESP_POCKET_AUDIO_AMP_ENABLED
static bool s_amp_initialized;
#endif
static bool s_amp_enabled;

#if ESP_POCKET_AUDIO_I2S_BITS_PER_SAMPLE == 32
#define ESP_POCKET_AUDIO_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_32BIT
#define ESP_POCKET_AUDIO_I2S_DATA_BITS_FOR_LOG 32
#elif ESP_POCKET_AUDIO_I2S_BITS_PER_SAMPLE == 24
#define ESP_POCKET_AUDIO_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_24BIT
#define ESP_POCKET_AUDIO_I2S_DATA_BITS_FOR_LOG 24
#else
#define ESP_POCKET_AUDIO_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#define ESP_POCKET_AUDIO_I2S_DATA_BITS_FOR_LOG 16
#endif

static uint64_t gpio_mask(gpio_num_t gpio)
{
    return gpio == GPIO_NUM_NC ? 0 : 1ULL << gpio;
}

static esp_err_t init_codec_address_straps(void)
{
#if ESP_POCKET_AUDIO_CODEC_I2C_ENABLED
    if (s_codec_address_straps_initialized) {
        return ESP_OK;
    }
    if (ESP_POCKET_AUDIO_CODEC_AD1_GPIO == GPIO_NUM_NC) {
        s_codec_address_straps_initialized = true;
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = gpio_mask(ESP_POCKET_AUDIO_CODEC_AD1_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = ESP_POCKET_AUDIO_CODEC_AD1_LEVEL ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = ESP_POCKET_AUDIO_CODEC_AD1_LEVEL ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "codec address GPIO config failed");
    ESP_LOGI(TAG, "codec AD1 GPIO%d weak-pulled %s",
             ESP_POCKET_AUDIO_CODEC_AD1_GPIO,
             ESP_POCKET_AUDIO_CODEC_AD1_LEVEL ? "high" : "low");
    s_codec_address_straps_initialized = true;
#endif
    return ESP_OK;
}

static esp_err_t init_codec_i2c(void)
{
#if ESP_POCKET_AUDIO_CODEC_I2C_ENABLED
    ESP_RETURN_ON_ERROR(init_codec_address_straps(), TAG, "codec address setup failed");

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

#if ESP_POCKET_AUDIO_DIAG_I2C_SCAN_ENABLED
    ESP_RETURN_ON_ERROR(diagnostics_i2c_scan_bus(
        s_hw.codec_i2c_bus,
        "codec I2C",
        ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS
    ), TAG, "codec I2C scan failed");
#endif

    esp_err_t probe = i2c_master_probe(
        s_hw.codec_i2c_bus,
        ESP_POCKET_AUDIO_CODEC_I2C_ADDR,
        ESP_POCKET_AUDIO_CODEC_I2C_TIMEOUT_MS
    );
    if (probe != ESP_OK) {
        ESP_LOGE(TAG,
                 "%s not detected at 7-bit address 0x%02x",
                 audio_codec_type_name(ESP_POCKET_AUDIO_CODEC_TYPE),
                 ESP_POCKET_AUDIO_CODEC_I2C_ADDR);
        return probe;
    }

    ESP_LOGI(TAG,
             "%s acknowledged at 7-bit address 0x%02x",
             audio_codec_type_name(ESP_POCKET_AUDIO_CODEC_TYPE),
             ESP_POCKET_AUDIO_CODEC_I2C_ADDR);
#else
    ESP_LOGI(TAG, "%s uses no codec-control I2C", audio_codec_type_name(ESP_POCKET_AUDIO_CODEC_TYPE));
#endif
    return ESP_OK;
}

static esp_err_t init_control_i2c(void)
{
#if ESP_POCKET_AUDIO_CONTROL_I2C_ENABLED
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
#else
    ESP_LOGI(TAG, "control I2C is disabled");
    return ESP_OK;
#endif
}

#if ESP_POCKET_AUDIO_CONTROL_I2C_ENABLED
static esp_err_t init_display(void)
{
#if ESP_POCKET_AUDIO_OLED_ENABLED
    uint8_t oled_addr = ESP_POCKET_AUDIO_OLED_ADDR;
    esp_err_t err = i2c_master_probe(
        s_hw.control_i2c_bus,
        oled_addr,
        ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS
    );
#if ESP_POCKET_AUDIO_OLED_TRY_ALT_ADDR
    if (err != ESP_OK && ESP_POCKET_AUDIO_OLED_ALT_ADDR != ESP_POCKET_AUDIO_OLED_ADDR) {
        ESP_LOGE(TAG, "OLED not detected at configured 7-bit address 0x%02x, trying 0x%02x",
                 ESP_POCKET_AUDIO_OLED_ADDR,
                 ESP_POCKET_AUDIO_OLED_ALT_ADDR);
        oled_addr = ESP_POCKET_AUDIO_OLED_ALT_ADDR;
        err = i2c_master_probe(
            s_hw.control_i2c_bus,
            oled_addr,
            ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS
        );
    }
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED not detected at configured 7-bit address 0x%02x", oled_addr);
        ESP_LOGI(TAG, "continuing without OLED display");
        return ESP_OK;
    }

    err = pocket_oled_init(
        &s_hw.display,
        s_hw.control_i2c_bus,
        oled_addr,
        ESP_POCKET_AUDIO_OLED_I2C_CLK_HZ
    );
    if (err == ESP_OK) {
        pocket_oled_draw_text_row(&s_hw.display, 0, "ESP-POCKET AUDIO");
        pocket_oled_draw_text_row(&s_hw.display, 2, "HARDWARE READY");
        pocket_oled_draw_text_row(&s_hw.display, 4, "CONTROLS READY");
        err = pocket_oled_flush(&s_hw.display);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "continuing without OLED display");
        return ESP_OK;
    }
#else
    ESP_LOGI(TAG, "OLED display is disabled");
#endif
    return ESP_OK;
}

static esp_err_t init_display_i2c(void *ctx)
{
    (void)ctx;
    return init_display();
}
#endif

static i2s_std_config_t i2s_standard_config(void)
{
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(ESP_POCKET_AUDIO_I2S_DATA_WIDTH, I2S_SLOT_MODE_STEREO),
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
    config.clk_cfg.clk_src = ESP_POCKET_AUDIO_I2S_CLK_SRC;
    return config;
}

static esp_err_t init_i2s_clocking(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ESP_POCKET_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = ESP_POCKET_AUDIO_I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = ESP_POCKET_AUDIO_I2S_DMA_FRAME_NUM;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg,
                                        &s_hw.i2s_tx,
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
                                        &s_hw.i2s_rx
#else
                                        NULL
#endif
                                        ), TAG, "I2S channel init failed");

    i2s_std_config_t std_cfg = i2s_standard_config();

    ESP_LOGI(TAG, "I2S: port=%d clk_src=%d mclk=%d bclk=%d ws=%d dout=%d din=%d rate=%d bits=%d dma=%ux%u",
             ESP_POCKET_AUDIO_I2S_PORT,
             ESP_POCKET_AUDIO_I2S_CLK_SRC,
             ESP_POCKET_AUDIO_I2S_MCLK_GPIO,
             ESP_POCKET_AUDIO_I2S_BCLK_GPIO,
             ESP_POCKET_AUDIO_I2S_WS_GPIO,
             ESP_POCKET_AUDIO_I2S_DOUT_GPIO,
             ESP_POCKET_AUDIO_I2S_DIN_GPIO,
             ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ,
             ESP_POCKET_AUDIO_I2S_DATA_BITS_FOR_LOG,
             (unsigned)chan_cfg.dma_desc_num,
             (unsigned)chan_cfg.dma_frame_num);

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_hw.i2s_tx, &std_cfg), TAG, "I2S std mode init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_hw.i2s_tx), TAG, "I2S enable failed");
    return ESP_OK;
}

#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
static esp_err_t start_i2s_input(void)
{
    i2s_std_config_t std_cfg = i2s_standard_config();
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_hw.i2s_rx, &std_cfg), TAG, "I2S RX std mode init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_hw.i2s_rx), TAG, "I2S RX enable failed");
    ESP_LOGI(TAG, "I2S microphone input enabled");
    return ESP_OK;
}
#endif

#if ESP_POCKET_AUDIO_AMP_ENABLED
static int amp_shdn_level_for_enabled(bool enabled)
{
#if ESP_POCKET_AUDIO_AMP_SHDN_ACTIVE_LOW
    return enabled ? 1 : 0;
#else
    return enabled ? 0 : 1;
#endif
}
#endif

esp_err_t esp_pocket_audio_amp_set_enabled(bool enabled)
{
#if ESP_POCKET_AUDIO_AMP_ENABLED
    if (!s_amp_initialized || ESP_POCKET_AUDIO_AMP_SHDN_GPIO == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(
        ESP_POCKET_AUDIO_AMP_SHDN_GPIO,
        amp_shdn_level_for_enabled(enabled)
    ), TAG, "amp SHDN GPIO set failed");
    s_amp_enabled = enabled;
    ESP_LOGI(TAG, "speaker amp %s", enabled ? "enabled" : "shutdown");
    return ESP_OK;
#else
    (void)enabled;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool esp_pocket_audio_amp_is_enabled(void)
{
    return s_amp_enabled;
}

esp_err_t esp_pocket_audio_mic_set_gain_db(float requested_db, float *applied_db)
{
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return audio_codec_set_mic_gain_db(&s_hw.codec, requested_db, applied_db);
#else
    (void)requested_db;
    (void)applied_db;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t init_amp_gpio(void)
{
#if ESP_POCKET_AUDIO_AMP_ENABLED
    if (ESP_POCKET_AUDIO_AMP_SHDN_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "speaker amp enabled but SHDN GPIO is not configured");
        return ESP_OK;
    }

    const bool default_enabled = ESP_POCKET_AUDIO_AMP_DEFAULT_ENABLED != 0;
    gpio_config_t cfg = {
        .pin_bit_mask = gpio_mask(ESP_POCKET_AUDIO_AMP_SHDN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = ESP_POCKET_AUDIO_AMP_SHDN_ACTIVE_LOW && default_enabled ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = ESP_POCKET_AUDIO_AMP_SHDN_ACTIVE_LOW && !default_enabled ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "amp SHDN GPIO config failed");
    s_amp_initialized = true;
    ESP_RETURN_ON_ERROR(esp_pocket_audio_amp_set_enabled(default_enabled), TAG, "amp SHDN default failed");
    ESP_LOGI(TAG,
             "speaker amp SHDN GPIO%d default=%s",
             ESP_POCKET_AUDIO_AMP_SHDN_GPIO,
             default_enabled ? "enabled" : "shutdown");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "speaker amp is not configured");
    return ESP_OK;
#endif
}

esp_err_t esp_pocket_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "board=%s codec=%s", ESP_POCKET_AUDIO_BOARD_NAME, audio_codec_type_name(ESP_POCKET_AUDIO_CODEC_TYPE));
    ESP_RETURN_ON_ERROR(init_amp_gpio(), TAG, "amp GPIO failed");
    ESP_RETURN_ON_ERROR(init_codec_address_straps(), TAG, "codec address setup failed");
    ESP_RETURN_ON_ERROR(init_i2s_clocking(), TAG, "I2S failed");
    ESP_RETURN_ON_ERROR(init_codec_i2c(), TAG, "codec I2C failed");
    ESP_RETURN_ON_ERROR(audio_codec_init(&s_hw.codec, s_hw.codec_i2c_bus), TAG, "codec init failed");
    ESP_RETURN_ON_ERROR(audio_codec_start(&s_hw.codec), TAG, "codec start failed");
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    ESP_RETURN_ON_ERROR(audio_codec_configure_mic_input(&s_hw.codec), TAG, "codec mic input failed");
#endif
    ESP_RETURN_ON_ERROR(audio_codec_set_volume(&s_hw.codec, ESP_POCKET_AUDIO_CODEC_DEFAULT_VOLUME_PERCENT),
                        TAG,
                        "codec volume failed");
#if ESP_POCKET_AUDIO_CODEC_START_MUTED
    ESP_RETURN_ON_ERROR(audio_codec_set_mute(&s_hw.codec, true), TAG, "codec mute failed");
    ESP_LOGI(TAG, "codec DAC muted at startup");
#endif
#if ESP_POCKET_AUDIO_MIC_INPUT_ENABLED
    ESP_RETURN_ON_ERROR(start_i2s_input(), TAG, "I2S microphone input failed");
#endif
    ESP_RETURN_ON_ERROR(init_control_i2c(), TAG, "control I2C failed");
#if ESP_POCKET_AUDIO_CONTROL_I2C_ENABLED
#if ESP_POCKET_AUDIO_DIAG_I2C_SCAN_ENABLED
    ESP_RETURN_ON_ERROR(diagnostics_i2c_scan_bus(
        s_hw.control_i2c_bus,
        "control I2C",
        ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS
    ), TAG, "control I2C scan failed");
#endif
    ESP_RETURN_ON_ERROR(control_i2c_service_start(s_hw.control_i2c_bus), TAG, "control I2C service failed");
    ESP_RETURN_ON_ERROR(controls_init(s_hw.control_i2c_bus), TAG, "controls init failed");
    ESP_RETURN_ON_ERROR(control_i2c_call_priority(
        CONTROL_I2C_PRIORITY_LOW,
        init_display_i2c,
        NULL,
        pdMS_TO_TICKS(500)
    ), TAG, "display init failed");
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "hardware init complete");
    return ESP_OK;
}

const esp_pocket_audio_hw_t *esp_pocket_audio_hw(void)
{
    return s_initialized ? &s_hw : NULL;
}
