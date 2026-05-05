#include "esp_pocket_audio_input.h"

#include <stddef.h>

#include "ads1115.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_pocket_audio_board_config.h"
#include "pcf8574.h"

#define EVENT_QUEUE_LEN 16
#define INPUT_SERVICE_TASK_STACK_SIZE 4096

typedef struct {
    bool is_gpio;
    uint8_t expander;
    uint8_t pin;
    gpio_num_t gpio;
} switch_mapping_t;

typedef struct {
    bool stable_pressed;
    bool sampled_pressed;
    TickType_t changed_at;
} switch_debounce_t;

static const char *TAG = "ESP_POCKET_AUDIO_INPUT";
static const TickType_t INPUT_SERVICE_PERIOD_TICKS = pdMS_TO_TICKS(20);
static const TickType_t SWITCH_HELD_LOG_TICKS = pdMS_TO_TICKS(500);

static const uint8_t PCF8574_ADDRS[] = {
    ESP_POCKET_AUDIO_PCF8574_0_ADDR,
};

static const switch_mapping_t SWITCH_MAP[ESP_POCKET_AUDIO_SWITCH_COUNT] = {
    [ESP_POCKET_AUDIO_SWITCH_CHORD_1] = {false, ESP_POCKET_AUDIO_SW_CHORD_1_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_1_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_2] = {false, ESP_POCKET_AUDIO_SW_CHORD_2_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_2_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_3] = {false, ESP_POCKET_AUDIO_SW_CHORD_3_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_3_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_4] = {false, ESP_POCKET_AUDIO_SW_CHORD_4_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_4_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_5] = {false, ESP_POCKET_AUDIO_SW_CHORD_5_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_5_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_6] = {false, ESP_POCKET_AUDIO_SW_CHORD_6_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_6_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_CHORD_7] = {false, ESP_POCKET_AUDIO_SW_CHORD_7_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_7_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_JOYSTICK_CLICK] = {true, 0, 0, ESP_POCKET_AUDIO_SW_JOYSTICK_CLICK_GPIO},
    [ESP_POCKET_AUDIO_SWITCH_KEY_TEMPO] = {false, ESP_POCKET_AUDIO_SW_KEY_TEMPO_EXPANDER, ESP_POCKET_AUDIO_SW_KEY_TEMPO_PIN, GPIO_NUM_NC},
    [ESP_POCKET_AUDIO_SWITCH_KEY_MODE] = {true, 0, 0, ESP_POCKET_AUDIO_SW_KEY_MODE_GPIO},
    [ESP_POCKET_AUDIO_SWITCH_KEY_SOUND] = {true, 0, 0, ESP_POCKET_AUDIO_SW_KEY_SOUND_GPIO},
};

static bool s_initialized;
static i2c_master_bus_handle_t s_input_i2c_bus;
static ads1115_t s_ads1115;
static pcf8574_t s_pcf8574[ESP_POCKET_AUDIO_PCF8574_COUNT];
static uint8_t s_pcf_port_state[ESP_POCKET_AUDIO_PCF8574_COUNT];
static switch_debounce_t s_switches[ESP_POCKET_AUDIO_SWITCH_COUNT];
static esp_pocket_audio_input_event_t s_events[EVENT_QUEUE_LEN];
static size_t s_event_head;
static size_t s_event_tail;
static esp_pocket_audio_joystick_state_t s_joystick;
static volatile uint32_t s_gpio_irq_pending_mask;
static uint32_t s_gpio_debounce_pending_mask;
static portMUX_TYPE s_event_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_gpio_irq_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_input_service_task;

static size_t configured_expander_count(void)
{
    if (ESP_POCKET_AUDIO_PCF8574_COUNT > 2) {
        return 2;
    }
    return ESP_POCKET_AUDIO_PCF8574_COUNT;
}

static const char *switch_name(esp_pocket_audio_switch_t sw)
{
    switch (sw) {
    case ESP_POCKET_AUDIO_SWITCH_CHORD_1:
        return "KEY_CHORD_1";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_2:
        return "KEY_CHORD_2";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_3:
        return "KEY_CHORD_3";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_4:
        return "KEY_CHORD_4";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_5:
        return "KEY_CHORD_5";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_6:
        return "KEY_CHORD_6";
    case ESP_POCKET_AUDIO_SWITCH_CHORD_7:
        return "KEY_CHORD_7";
    case ESP_POCKET_AUDIO_SWITCH_JOYSTICK_CLICK:
        return "JOYSTICK_SW";
    case ESP_POCKET_AUDIO_SWITCH_KEY_TEMPO:
        return "KEY_TEMPO";
    case ESP_POCKET_AUDIO_SWITCH_KEY_MODE:
        return "KEY_MODE";
    case ESP_POCKET_AUDIO_SWITCH_KEY_SOUND:
        return "KEY_SOUND";
    default:
        return "UNKNOWN";
    }
}

static esp_err_t probe_i2c_addr(uint8_t address)
{
    return i2c_master_probe(
        s_input_i2c_bus,
        address,
        ESP_POCKET_AUDIO_INPUT_I2C_TIMEOUT_MS
    );
}

static void scan_input_i2c_bus(void)
{
    bool any_found = false;
    esp_log_level_t previous_i2c_log_level = esp_log_level_get("i2c.master");

    ESP_LOGI(TAG, "scanning input I2C bus for ADS1115/PCF8574 diagnostics");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (probe_i2c_addr(addr) == ESP_OK) {
            ESP_LOGI(TAG, "input I2C device found at 7-bit address 0x%02x", addr);
            any_found = true;
        }
    }
    esp_log_level_set("i2c.master", previous_i2c_log_level);

    if (!any_found) {
        ESP_LOGW(TAG, "input I2C scan found no devices");
    }
}

static esp_err_t init_interrupt_pin(void)
{
    if (ESP_POCKET_AUDIO_PCF8574_INT_GPIO == GPIO_NUM_NC) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << ESP_POCKET_AUDIO_PCF8574_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

static esp_err_t init_gpio_switches(void)
{
    uint64_t gpio_mask = 0;
    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT; ++i) {
        if (SWITCH_MAP[i].is_gpio && SWITCH_MAP[i].gpio != GPIO_NUM_NC) {
            gpio_mask |= 1ULL << SWITCH_MAP[i].gpio;
        }
    }

    if (gpio_mask == 0) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = gpio_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    return gpio_config(&cfg);
}

static void IRAM_ATTR gpio_switch_isr(void *arg)
{
    uint32_t sw = (uint32_t)(uintptr_t)arg;
    portENTER_CRITICAL_ISR(&s_gpio_irq_mux);
    s_gpio_irq_pending_mask |= 1UL << sw;
    portEXIT_CRITICAL_ISR(&s_gpio_irq_mux);
}

static esp_err_t init_gpio_switch_interrupts(void)
{
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service init failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT; ++i) {
        const switch_mapping_t *mapping = &SWITCH_MAP[i];
        if (!mapping->is_gpio || mapping->gpio == GPIO_NUM_NC) {
            continue;
        }

        err = gpio_isr_handler_add(mapping->gpio, gpio_switch_isr, (void *)(uintptr_t)i);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d ISR handler init failed: %s",
                     mapping->gpio,
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "GPIO%d switch interrupt enabled for %u",
                 mapping->gpio,
                 (unsigned)i);
    }

    return ESP_OK;
}

static void push_event(esp_pocket_audio_switch_t sw, bool pressed)
{
    bool dropped_oldest = false;

    portENTER_CRITICAL(&s_event_mux);
    size_t next_head = (s_event_head + 1) % EVENT_QUEUE_LEN;
    if (next_head == s_event_tail) {
        s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_LEN;
        dropped_oldest = true;
    }

    s_events[s_event_head].sw = sw;
    s_events[s_event_head].type = pressed ?
        ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_PRESSED :
        ESP_POCKET_AUDIO_INPUT_EVENT_SWITCH_RELEASED;
    s_event_head = next_head;
    portEXIT_CRITICAL(&s_event_mux);

    if (dropped_oldest) {
        ESP_LOGW(TAG, "Input event queue full, dropping oldest event");
    }
}

static bool mapping_is_available(const switch_mapping_t *mapping)
{
    if (mapping->is_gpio) {
        return mapping->gpio != GPIO_NUM_NC;
    }
    return mapping->expander < configured_expander_count() &&
           mapping->pin < 8;
}

static bool raw_switch_pressed(const switch_mapping_t *mapping)
{
    if (!mapping_is_available(mapping)) {
        return false;
    }

    bool level = mapping->is_gpio ?
        gpio_get_level(mapping->gpio) != 0 :
        ((s_pcf_port_state[mapping->expander] >> mapping->pin) & 0x01) != 0;
#if ESP_POCKET_AUDIO_SWITCH_ACTIVE_LOW
    return !level;
#else
    return level;
#endif
}

static float normalize_axis(int16_t raw)
{
    int32_t centered = (int32_t)raw - ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW;
    int32_t deadzone = ESP_POCKET_AUDIO_ADS1115_DEADZONE_RAW;

    if (centered >= -deadzone && centered <= deadzone) {
        return 0.0f;
    }

    if (centered > 0) {
        int32_t span = ESP_POCKET_AUDIO_ADS1115_AXIS_MAX_RAW - ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW - deadzone;
        if (span <= 0) {
            return 0.0f;
        }
        float value = (float)(centered - deadzone) / (float)span;
        return value > 1.0f ? 1.0f : value;
    }

    int32_t span = ESP_POCKET_AUDIO_ADS1115_AXIS_CENTER_RAW - ESP_POCKET_AUDIO_ADS1115_AXIS_MIN_RAW - deadzone;
    if (span <= 0) {
        return 0.0f;
    }
    float value = (float)(centered + deadzone) / (float)span;
    return value < -1.0f ? -1.0f : value;
}

static void debounce_switch(esp_pocket_audio_switch_t sw, bool pressed, TickType_t now_ticks)
{
    switch_debounce_t *debounce = &s_switches[sw];

    if (pressed != debounce->sampled_pressed) {
        debounce->sampled_pressed = pressed;
        debounce->changed_at = now_ticks;
        if (SWITCH_MAP[sw].is_gpio) {
            s_gpio_debounce_pending_mask |= 1UL << sw;
        }
        return;
    }

    TickType_t debounce_ticks = pdMS_TO_TICKS(ESP_POCKET_AUDIO_SWITCH_DEBOUNCE_MS);
    if (pressed != debounce->stable_pressed && (now_ticks - debounce->changed_at) >= debounce_ticks) {
        debounce->stable_pressed = pressed;
        push_event(sw, pressed);
        ESP_LOGV(TAG, "%s %s", switch_name(sw), pressed ? "pressed" : "released");
    }

    if (pressed == debounce->stable_pressed && SWITCH_MAP[sw].is_gpio) {
        s_gpio_debounce_pending_mask &= ~(1UL << sw);
    }
}

static esp_err_t update_pcf_switches(TickType_t now_ticks)
{
    size_t expander_count = configured_expander_count();
    for (size_t i = 0; i < expander_count; ++i) {
        esp_err_t err = pcf8574_read_port(&s_pcf8574[i], &s_pcf_port_state[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PCF8574 %u read failed: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT; ++i) {
        if (SWITCH_MAP[i].is_gpio) {
            continue;
        }

        bool pressed = raw_switch_pressed(&SWITCH_MAP[i]);
        debounce_switch((esp_pocket_audio_switch_t)i, pressed, now_ticks);
    }

    return ESP_OK;
}

static void update_gpio_switches(TickType_t now_ticks)
{
    portENTER_CRITICAL(&s_gpio_irq_mux);
    uint32_t pending_mask = s_gpio_irq_pending_mask;
    s_gpio_irq_pending_mask = 0;
    portEXIT_CRITICAL(&s_gpio_irq_mux);

    pending_mask |= s_gpio_debounce_pending_mask;

    while (pending_mask != 0) {
        uint32_t sw = __builtin_ctz(pending_mask);
        pending_mask &= ~(1UL << sw);
        if (sw >= ESP_POCKET_AUDIO_SWITCH_COUNT) {
            continue;
        }

        const switch_mapping_t *mapping = &SWITCH_MAP[sw];
        if (!mapping->is_gpio || mapping->gpio == GPIO_NUM_NC) {
            continue;
        }

        bool pressed = raw_switch_pressed(mapping);
        debounce_switch((esp_pocket_audio_switch_t)sw, pressed, now_ticks);
    }
}

static esp_err_t update_joystick(void)
{
    int16_t raw_x = 0;
    int16_t raw_y = 0;

    esp_err_t err = ads1115_read_single_ended(
        &s_ads1115,
        (ads1115_channel_t)ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_CH,
        &raw_x
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADS1115 X read failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ads1115_read_single_ended(
        &s_ads1115,
        (ads1115_channel_t)ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_CH,
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
    s_joystick.direction = esp_pocket_audio_joystick_direction_from_axes(
        s_joystick.x.value,
        s_joystick.y.value
    );

    return ESP_OK;
}

static void log_pressed_switches(uint32_t pressed_mask)
{
    char line[160];
    size_t used = 0;

    used += snprintf(line + used, sizeof(line) - used, "pressed switches:");
    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT && used < sizeof(line); ++i) {
        if ((pressed_mask & (1UL << i)) == 0) {
            continue;
        }
        used += snprintf(line + used, sizeof(line) - used, " %s", switch_name((esp_pocket_audio_switch_t)i));
    }

    ESP_LOGV(TAG, "%s", line);
}

static void input_service_task(void *arg)
{
    (void)arg;

    TickType_t next_held_log = 0;
    esp_pocket_audio_joystick_direction_t last_direction = ESP_POCKET_AUDIO_JOYSTICK_CENTER;

    ESP_LOGI(TAG, "input service task started");

    while (true) {
        TickType_t now = xTaskGetTickCount();
        esp_err_t err = esp_pocket_audio_input_update();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "input service update failed: %s", esp_err_to_name(err));
        }

        esp_pocket_audio_joystick_state_t joystick = {0};
        if (esp_pocket_audio_joystick_read(&joystick) == ESP_OK && joystick.direction != last_direction) {
            ESP_LOGV(
                TAG,
                "joystick direction=%s x raw=%d norm=%.3f y raw=%d norm=%.3f",
                esp_pocket_audio_joystick_direction_name(joystick.direction),
                joystick.x.raw,
                joystick.x.value,
                joystick.y.raw,
                joystick.y.value
            );
            last_direction = joystick.direction;
        }

        uint32_t pressed_mask = esp_pocket_audio_switch_pressed_mask();
        if (pressed_mask != 0 && (int32_t)(now - next_held_log) >= 0) {
            log_pressed_switches(pressed_mask);
            next_held_log = now + SWITCH_HELD_LOG_TICKS;
        }

        vTaskDelay(INPUT_SERVICE_PERIOD_TICKS);
    }
}

static esp_err_t start_input_service_task(void)
{
    if (s_input_service_task != NULL) {
        return ESP_OK;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        input_service_task,
        "input_service",
        INPUT_SERVICE_TASK_STACK_SIZE,
        NULL,
        4,
        &s_input_service_task,
        0
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_pocket_audio_input_init(i2c_master_bus_handle_t bus)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_input_i2c_bus = bus;
    scan_input_i2c_bus();

    esp_err_t err = init_interrupt_pin();
    if (err != ESP_OK) {
        return err;
    }

    err = probe_i2c_addr(ESP_POCKET_AUDIO_ADS1115_ADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 not detected at configured 7-bit address 0x%02x", ESP_POCKET_AUDIO_ADS1115_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    err = ads1115_init(
        &s_ads1115,
        s_input_i2c_bus,
        ESP_POCKET_AUDIO_ADS1115_ADDR,
        ESP_POCKET_AUDIO_INPUT_I2C_CLK_HZ,
        ESP_POCKET_AUDIO_INPUT_I2C_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 device handle init failed at 0x%02x: %s",
                 ESP_POCKET_AUDIO_ADS1115_ADDR,
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ADS1115 configured at 7-bit address 0x%02x", ESP_POCKET_AUDIO_ADS1115_ADDR);

    size_t expander_count = configured_expander_count();
    for (size_t i = 0; i < expander_count; ++i) {
        err = probe_i2c_addr(PCF8574_ADDRS[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PCF8574 %u not detected at configured 7-bit address 0x%02x",
                     (unsigned)i,
                     PCF8574_ADDRS[i]);
            return ESP_ERR_NOT_FOUND;
        }

        err = pcf8574_init(
            &s_pcf8574[i],
            s_input_i2c_bus,
            PCF8574_ADDRS[i],
            ESP_POCKET_AUDIO_INPUT_I2C_CLK_HZ,
            ESP_POCKET_AUDIO_INPUT_I2C_TIMEOUT_MS
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PCF8574 %u init failed at 0x%02x: %s",
                     (unsigned)i,
                     PCF8574_ADDRS[i],
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "PCF8574 %u configured at 7-bit address 0x%02x",
                 (unsigned)i,
                 PCF8574_ADDRS[i]);
    }

    err = init_gpio_switches();
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < configured_expander_count(); ++i) {
        s_pcf_port_state[i] = 0xFF;
    }

    TickType_t now = xTaskGetTickCount();
    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT; ++i) {
        bool pressed = SWITCH_MAP[i].is_gpio ? raw_switch_pressed(&SWITCH_MAP[i]) : false;
        s_switches[i].stable_pressed = pressed;
        s_switches[i].sampled_pressed = pressed;
        s_switches[i].changed_at = now;
    }

    err = init_gpio_switch_interrupts();
    if (err != ESP_OK) {
        return err;
    }

    s_event_head = 0;
    s_event_tail = 0;
    s_initialized = true;

    err = esp_pocket_audio_input_update();
    if (err != ESP_OK) {
        return err;
    }

    return start_input_service_task();
}

esp_err_t esp_pocket_audio_input_update(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t now = xTaskGetTickCount();
    esp_err_t switch_err = update_pcf_switches(now);
    update_gpio_switches(now);
    esp_err_t joystick_err = update_joystick();

    return switch_err != ESP_OK ? switch_err : joystick_err;
}

bool esp_pocket_audio_input_int_is_asserted(void)
{
    if (ESP_POCKET_AUDIO_PCF8574_INT_GPIO == GPIO_NUM_NC) {
        return false;
    }

    return gpio_get_level(ESP_POCKET_AUDIO_PCF8574_INT_GPIO) == 0;
}

gpio_num_t esp_pocket_audio_input_int_gpio(void)
{
    return ESP_POCKET_AUDIO_PCF8574_INT_GPIO;
}

bool esp_pocket_audio_switch_is_pressed(esp_pocket_audio_switch_t sw)
{
    if (sw >= ESP_POCKET_AUDIO_SWITCH_COUNT) {
        return false;
    }
    return s_switches[sw].stable_pressed;
}

uint32_t esp_pocket_audio_switch_pressed_mask(void)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < ESP_POCKET_AUDIO_SWITCH_COUNT; ++i) {
        if (s_switches[i].stable_pressed) {
            mask |= 1UL << i;
        }
    }
    return mask;
}

esp_err_t esp_pocket_audio_input_pop_event(esp_pocket_audio_input_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_event_mux);
    if (s_event_tail == s_event_head) {
        portEXIT_CRITICAL(&s_event_mux);
        return ESP_ERR_NOT_FOUND;
    }

    *event = s_events[s_event_tail];
    s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_LEN;
    portEXIT_CRITICAL(&s_event_mux);
    return ESP_OK;
}

esp_err_t esp_pocket_audio_axis_read(esp_pocket_audio_axis_t axis, esp_pocket_audio_axis_state_t *state)
{
    if (state == NULL || axis >= ESP_POCKET_AUDIO_AXIS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = axis == ESP_POCKET_AUDIO_AXIS_X ? s_joystick.x : s_joystick.y;
    return ESP_OK;
}

esp_err_t esp_pocket_audio_joystick_read(esp_pocket_audio_joystick_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_joystick;
    return ESP_OK;
}
