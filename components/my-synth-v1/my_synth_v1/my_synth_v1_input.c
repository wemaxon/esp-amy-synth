#include "my_synth_v1_input.h"

#include <stddef.h>

#include "ads1115.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_synth_v1_board_config.h"
#include "pcf8574.h"

#define EVENT_QUEUE_LEN 16

typedef struct {
    uint8_t expander;
    uint8_t pin;
} switch_mapping_t;

typedef struct {
    bool stable_pressed;
    bool sampled_pressed;
    TickType_t changed_at;
} switch_debounce_t;

static const char *TAG = "MY_SYNTH_V1_INPUT";

static const uint8_t PCF8574_ADDRS[] = {
    MY_SYNTH_V1_PCF8574_0_ADDR,
    MY_SYNTH_V1_PCF8574_1_ADDR,
};

static const switch_mapping_t SWITCH_MAP[MY_SYNTH_V1_SWITCH_COUNT] = {
    [MY_SYNTH_V1_SWITCH_CHORD_1] = {MY_SYNTH_V1_SW_CHORD_1_EXPANDER, MY_SYNTH_V1_SW_CHORD_1_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_2] = {MY_SYNTH_V1_SW_CHORD_2_EXPANDER, MY_SYNTH_V1_SW_CHORD_2_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_3] = {MY_SYNTH_V1_SW_CHORD_3_EXPANDER, MY_SYNTH_V1_SW_CHORD_3_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_4] = {MY_SYNTH_V1_SW_CHORD_4_EXPANDER, MY_SYNTH_V1_SW_CHORD_4_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_5] = {MY_SYNTH_V1_SW_CHORD_5_EXPANDER, MY_SYNTH_V1_SW_CHORD_5_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_6] = {MY_SYNTH_V1_SW_CHORD_6_EXPANDER, MY_SYNTH_V1_SW_CHORD_6_PIN},
    [MY_SYNTH_V1_SWITCH_CHORD_7] = {MY_SYNTH_V1_SW_CHORD_7_EXPANDER, MY_SYNTH_V1_SW_CHORD_7_PIN},
    [MY_SYNTH_V1_SWITCH_JOYSTICK_CLICK] = {MY_SYNTH_V1_SW_JOYSTICK_CLICK_EXPANDER, MY_SYNTH_V1_SW_JOYSTICK_CLICK_PIN},
    [MY_SYNTH_V1_SWITCH_KEY_MENU] = {MY_SYNTH_V1_SW_KEY_MENU_EXPANDER, MY_SYNTH_V1_SW_KEY_MENU_PIN},
    [MY_SYNTH_V1_SWITCH_TEMPO_MENU] = {MY_SYNTH_V1_SW_TEMPO_MENU_EXPANDER, MY_SYNTH_V1_SW_TEMPO_MENU_PIN},
    [MY_SYNTH_V1_SWITCH_SOUND_MENU] = {MY_SYNTH_V1_SW_SOUND_MENU_EXPANDER, MY_SYNTH_V1_SW_SOUND_MENU_PIN},
};

static bool s_initialized;
static ads1115_t s_ads1115;
static pcf8574_t s_pcf8574[2];
static uint8_t s_pcf_port_state[2] = {0xFF, 0xFF};
static switch_debounce_t s_switches[MY_SYNTH_V1_SWITCH_COUNT];
static my_synth_v1_input_event_t s_events[EVENT_QUEUE_LEN];
static size_t s_event_head;
static size_t s_event_tail;
static my_synth_v1_joystick_state_t s_joystick;

static size_t configured_expander_count(void)
{
    if (MY_SYNTH_V1_PCF8574_COUNT > 2) {
        return 2;
    }
    return MY_SYNTH_V1_PCF8574_COUNT;
}

static esp_err_t init_i2c_bus(void)
{
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MY_SYNTH_V1_INPUT_I2C_SDA_GPIO,
        .scl_io_num = MY_SYNTH_V1_INPUT_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MY_SYNTH_V1_INPUT_I2C_CLK_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(MY_SYNTH_V1_INPUT_I2C_PORT, &i2c_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(MY_SYNTH_V1_INPUT_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Input I2C bus already installed");
        return ESP_OK;
    }
    return err;
}

static esp_err_t init_interrupt_pin(void)
{
    if (MY_SYNTH_V1_PCF8574_INT_GPIO == GPIO_NUM_NC) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << MY_SYNTH_V1_PCF8574_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

static void push_event(my_synth_v1_switch_t sw, bool pressed)
{
    size_t next_head = (s_event_head + 1) % EVENT_QUEUE_LEN;
    if (next_head == s_event_tail) {
        s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_LEN;
        ESP_LOGW(TAG, "Input event queue full, dropping oldest event");
    }

    s_events[s_event_head].sw = sw;
    s_events[s_event_head].type = pressed ?
        MY_SYNTH_V1_INPUT_EVENT_SWITCH_PRESSED :
        MY_SYNTH_V1_INPUT_EVENT_SWITCH_RELEASED;
    s_event_head = next_head;
}

static bool mapping_is_available(const switch_mapping_t *mapping)
{
    return mapping->expander < configured_expander_count() && mapping->pin < 8;
}

static bool raw_switch_pressed(const switch_mapping_t *mapping)
{
    if (!mapping_is_available(mapping)) {
        return false;
    }

    bool level = ((s_pcf_port_state[mapping->expander] >> mapping->pin) & 0x01) != 0;
#if MY_SYNTH_V1_SWITCH_ACTIVE_LOW
    return !level;
#else
    return level;
#endif
}

static float normalize_axis(int16_t raw)
{
    int32_t centered = (int32_t)raw - MY_SYNTH_V1_ADS1115_AXIS_CENTER_RAW;
    int32_t deadzone = MY_SYNTH_V1_ADS1115_DEADZONE_RAW;

    if (centered >= -deadzone && centered <= deadzone) {
        return 0.0f;
    }

    if (centered > 0) {
        int32_t span = MY_SYNTH_V1_ADS1115_AXIS_MAX_RAW - MY_SYNTH_V1_ADS1115_AXIS_CENTER_RAW - deadzone;
        if (span <= 0) {
            return 0.0f;
        }
        float value = (float)(centered - deadzone) / (float)span;
        return value > 1.0f ? 1.0f : value;
    }

    int32_t span = MY_SYNTH_V1_ADS1115_AXIS_CENTER_RAW - MY_SYNTH_V1_ADS1115_AXIS_MIN_RAW - deadzone;
    if (span <= 0) {
        return 0.0f;
    }
    float value = (float)(centered + deadzone) / (float)span;
    return value < -1.0f ? -1.0f : value;
}

static esp_err_t update_switches(TickType_t now_ticks)
{
    size_t expander_count = configured_expander_count();
    for (size_t i = 0; i < expander_count; ++i) {
        esp_err_t err = pcf8574_read_port(&s_pcf8574[i], &s_pcf_port_state[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PCF8574 %u read failed: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    TickType_t debounce_ticks = pdMS_TO_TICKS(MY_SYNTH_V1_SWITCH_DEBOUNCE_MS);
    for (size_t i = 0; i < MY_SYNTH_V1_SWITCH_COUNT; ++i) {
        bool pressed = raw_switch_pressed(&SWITCH_MAP[i]);
        switch_debounce_t *debounce = &s_switches[i];

        if (pressed != debounce->sampled_pressed) {
            debounce->sampled_pressed = pressed;
            debounce->changed_at = now_ticks;
            continue;
        }

        if (pressed != debounce->stable_pressed && (now_ticks - debounce->changed_at) >= debounce_ticks) {
            debounce->stable_pressed = pressed;
            push_event((my_synth_v1_switch_t)i, pressed);
        }
    }

    return ESP_OK;
}

static esp_err_t update_joystick(void)
{
    int16_t raw_x = 0;
    int16_t raw_y = 0;

    esp_err_t err = ads1115_read_single_ended(
        &s_ads1115,
        (ads1115_channel_t)MY_SYNTH_V1_ADS1115_JOYSTICK_X_CH,
        &raw_x
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADS1115 X read failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ads1115_read_single_ended(
        &s_ads1115,
        (ads1115_channel_t)MY_SYNTH_V1_ADS1115_JOYSTICK_Y_CH,
        &raw_y
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADS1115 Y read failed: %s", esp_err_to_name(err));
        return err;
    }

    s_joystick.x.raw = raw_x;
    s_joystick.x.value = normalize_axis(raw_x);
    s_joystick.y.raw = raw_y;
    s_joystick.y.value = normalize_axis(raw_y);

    return ESP_OK;
}

esp_err_t my_synth_v1_input_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = init_i2c_bus();
    if (err != ESP_OK) {
        return err;
    }

    err = init_interrupt_pin();
    if (err != ESP_OK) {
        return err;
    }

    err = ads1115_init(
        &s_ads1115,
        MY_SYNTH_V1_INPUT_I2C_PORT,
        MY_SYNTH_V1_ADS1115_ADDR,
        MY_SYNTH_V1_INPUT_I2C_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        return err;
    }

    size_t expander_count = configured_expander_count();
    for (size_t i = 0; i < expander_count; ++i) {
        err = pcf8574_init(
            &s_pcf8574[i],
            MY_SYNTH_V1_INPUT_I2C_PORT,
            PCF8574_ADDRS[i],
            MY_SYNTH_V1_INPUT_I2C_TIMEOUT_MS
        );
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PCF8574 %u init failed: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    TickType_t now = xTaskGetTickCount();
    for (size_t i = 0; i < MY_SYNTH_V1_SWITCH_COUNT; ++i) {
        s_switches[i].stable_pressed = false;
        s_switches[i].sampled_pressed = false;
        s_switches[i].changed_at = now;
    }

    s_event_head = 0;
    s_event_tail = 0;
    s_initialized = true;

    return my_synth_v1_input_update();
}

esp_err_t my_synth_v1_input_update(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t switch_err = update_switches(xTaskGetTickCount());
    esp_err_t joystick_err = update_joystick();

    return switch_err != ESP_OK ? switch_err : joystick_err;
}

bool my_synth_v1_input_int_is_asserted(void)
{
    if (MY_SYNTH_V1_PCF8574_INT_GPIO == GPIO_NUM_NC) {
        return false;
    }

    return gpio_get_level(MY_SYNTH_V1_PCF8574_INT_GPIO) == 0;
}

gpio_num_t my_synth_v1_input_int_gpio(void)
{
    return MY_SYNTH_V1_PCF8574_INT_GPIO;
}

bool my_synth_v1_switch_is_pressed(my_synth_v1_switch_t sw)
{
    if (sw >= MY_SYNTH_V1_SWITCH_COUNT) {
        return false;
    }
    return s_switches[sw].stable_pressed;
}

uint32_t my_synth_v1_switch_pressed_mask(void)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < MY_SYNTH_V1_SWITCH_COUNT; ++i) {
        if (s_switches[i].stable_pressed) {
            mask |= 1UL << i;
        }
    }
    return mask;
}

esp_err_t my_synth_v1_input_pop_event(my_synth_v1_input_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_event_tail == s_event_head) {
        return ESP_ERR_NOT_FOUND;
    }

    *event = s_events[s_event_tail];
    s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_LEN;
    return ESP_OK;
}

esp_err_t my_synth_v1_axis_read(my_synth_v1_axis_t axis, my_synth_v1_axis_state_t *state)
{
    if (state == NULL || axis >= MY_SYNTH_V1_AXIS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = axis == MY_SYNTH_V1_AXIS_X ? s_joystick.x : s_joystick.y;
    return ESP_OK;
}

esp_err_t my_synth_v1_joystick_read(my_synth_v1_joystick_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_joystick;
    return ESP_OK;
}
