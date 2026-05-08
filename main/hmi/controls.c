#include "hmi/controls.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "ads1115.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/control_i2c.h"
#include "hmi/joystick_calibration.h"
#include "pcf8574.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EVENT_QUEUE_LEN 16
#define CONTROLS_TASK_STACK_SIZE 4096

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

static const char *TAG = "CONTROLS";
static const TickType_t SERVICE_PERIOD_TICKS = pdMS_TO_TICKS(20);
static const TickType_t HELD_LOG_TICKS = pdMS_TO_TICKS(500);
static const float DIRECTION_DEADZONE = 0.35f;

static const uint8_t PCF8574_ADDRS[] = {
    ESP_POCKET_AUDIO_PCF8574_0_ADDR,
};

static const switch_mapping_t SWITCH_MAP[CONTROL_SWITCH_COUNT] = {
    [CONTROL_CHORD_1] = {false, ESP_POCKET_AUDIO_SW_CHORD_1_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_1_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_2] = {false, ESP_POCKET_AUDIO_SW_CHORD_2_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_2_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_3] = {false, ESP_POCKET_AUDIO_SW_CHORD_3_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_3_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_4] = {false, ESP_POCKET_AUDIO_SW_CHORD_4_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_4_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_5] = {false, ESP_POCKET_AUDIO_SW_CHORD_5_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_5_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_6] = {false, ESP_POCKET_AUDIO_SW_CHORD_6_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_6_PIN, GPIO_NUM_NC},
    [CONTROL_CHORD_7] = {false, ESP_POCKET_AUDIO_SW_CHORD_7_EXPANDER, ESP_POCKET_AUDIO_SW_CHORD_7_PIN, GPIO_NUM_NC},
    [CONTROL_JOYSTICK_CLICK] = {true, 0, 0, ESP_POCKET_AUDIO_SW_JOYSTICK_CLICK_GPIO},
    [CONTROL_KEY_MENU] = {true, 0, 0, ESP_POCKET_AUDIO_SW_MODE_MENU_GPIO},
    [CONTROL_MODE_MENU] = {false, ESP_POCKET_AUDIO_SW_KEY_MENU_EXPANDER, ESP_POCKET_AUDIO_SW_KEY_MENU_PIN, GPIO_NUM_NC},
    [CONTROL_SOUND_MENU] = {true, 0, 0, ESP_POCKET_AUDIO_SW_SOUND_MENU_GPIO},
};

static bool s_initialized;
static i2c_master_bus_handle_t s_i2c_bus;
static ads1115_t s_ads1115;
static pcf8574_t s_pcf8574[ESP_POCKET_AUDIO_PCF8574_COUNT];
static uint8_t s_pcf_port_state[ESP_POCKET_AUDIO_PCF8574_COUNT];
static switch_debounce_t s_switches[CONTROL_SWITCH_COUNT];
static control_event_t s_events[EVENT_QUEUE_LEN];
static size_t s_event_head;
static size_t s_event_tail;
static control_joystick_state_t s_joystick;
static control_wheel_state_t s_wheel;
static volatile uint32_t s_gpio_irq_pending_mask;
static uint32_t s_gpio_debounce_pending_mask;
static TickType_t s_next_io_error_log;
static portMUX_TYPE s_event_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_gpio_irq_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_service_task;

static size_t configured_expander_count(void)
{
    return ESP_POCKET_AUDIO_PCF8574_COUNT > 2 ? 2 : ESP_POCKET_AUDIO_PCF8574_COUNT;
}

const char *controls_switch_name(control_switch_t sw)
{
    switch (sw) {
    case CONTROL_CHORD_1:
        return "CHORD_1_TONIC";
    case CONTROL_CHORD_2:
        return "CHORD_2_SUPERTONIC";
    case CONTROL_CHORD_3:
        return "CHORD_3_MEDIANT";
    case CONTROL_CHORD_4:
        return "CHORD_4_SUBDOMINANT";
    case CONTROL_CHORD_5:
        return "CHORD_5_DOMINANT";
    case CONTROL_CHORD_6:
        return "CHORD_6_SUBMEDIANT";
    case CONTROL_CHORD_7:
        return "CHORD_7_LEADING";
    case CONTROL_JOYSTICK_CLICK:
        return "JOY_CLICK";
    case CONTROL_KEY_MENU:
        return "KEY_MENU";
    case CONTROL_MODE_MENU:
        return "MODE_MENU";
    case CONTROL_SOUND_MENU:
        return "SOUND_MENU";
    default:
        return "UNKNOWN";
    }
}

const char *controls_switch_short_name(control_switch_t sw)
{
    switch (sw) {
    case CONTROL_CHORD_1:
        return "1";
    case CONTROL_CHORD_2:
        return "2";
    case CONTROL_CHORD_3:
        return "3";
    case CONTROL_CHORD_4:
        return "4";
    case CONTROL_CHORD_5:
        return "5";
    case CONTROL_CHORD_6:
        return "6";
    case CONTROL_CHORD_7:
        return "7";
    case CONTROL_JOYSTICK_CLICK:
        return "J";
    case CONTROL_KEY_MENU:
        return "K";
    case CONTROL_MODE_MENU:
        return "M";
    case CONTROL_SOUND_MENU:
        return "S";
    default:
        return "?";
    }
}

const char *controls_joystick_direction_name(control_joystick_direction_t direction)
{
    switch (direction) {
    case CONTROL_JOYSTICK_CENTER:
        return "CENTER";
    case CONTROL_JOYSTICK_UP:
        return "UP";
    case CONTROL_JOYSTICK_UP_RIGHT:
        return "UP_RIGHT";
    case CONTROL_JOYSTICK_RIGHT:
        return "RIGHT";
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        return "DOWN_RIGHT";
    case CONTROL_JOYSTICK_DOWN:
        return "DOWN";
    case CONTROL_JOYSTICK_DOWN_LEFT:
        return "DOWN_LEFT";
    case CONTROL_JOYSTICK_LEFT:
        return "LEFT";
    case CONTROL_JOYSTICK_UP_LEFT:
        return "UP_LEFT";
    default:
        return "UNKNOWN";
    }
}

control_joystick_direction_t controls_joystick_direction_from_axes(float x, float y)
{
    float magnitude_squared = (x * x) + (y * y);
    if (magnitude_squared < (DIRECTION_DEADZONE * DIRECTION_DEADZONE)) {
        return CONTROL_JOYSTICK_CENTER;
    }

    float angle = atan2f(y, x);
    int octant = (int)floorf(((angle + (float)M_PI) / ((float)M_PI / 4.0f)) + 0.5f) & 0x07;

    switch (octant) {
    case 0:
        return CONTROL_JOYSTICK_LEFT;
    case 1:
        return CONTROL_JOYSTICK_DOWN_LEFT;
    case 2:
        return CONTROL_JOYSTICK_DOWN;
    case 3:
        return CONTROL_JOYSTICK_DOWN_RIGHT;
    case 4:
        return CONTROL_JOYSTICK_RIGHT;
    case 5:
        return CONTROL_JOYSTICK_UP_RIGHT;
    case 6:
        return CONTROL_JOYSTICK_UP;
    case 7:
    default:
        return CONTROL_JOYSTICK_UP_LEFT;
    }
}

static esp_err_t probe_i2c_addr(uint8_t address)
{
    return i2c_master_probe(s_i2c_bus, address, ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS);
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
    for (size_t i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
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

static esp_err_t init_gpio_interrupts(void)
{
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    for (size_t i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
        const switch_mapping_t *mapping = &SWITCH_MAP[i];
        if (!mapping->is_gpio || mapping->gpio == GPIO_NUM_NC) {
            continue;
        }
        err = gpio_isr_handler_add(mapping->gpio, gpio_switch_isr, (void *)(uintptr_t)i);
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(TAG, "GPIO%d interrupt enabled for %s", mapping->gpio, controls_switch_name((control_switch_t)i));
    }

    return ESP_OK;
}

static void push_event(control_switch_t sw, bool pressed)
{
    portENTER_CRITICAL(&s_event_mux);
    size_t next_head = (s_event_head + 1) % EVENT_QUEUE_LEN;
    if (next_head == s_event_tail) {
        s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_LEN;
    }
    s_events[s_event_head].sw = sw;
    s_events[s_event_head].type = pressed ? CONTROL_EVENT_SWITCH_PRESSED : CONTROL_EVENT_SWITCH_RELEASED;
    s_event_head = next_head;
    portEXIT_CRITICAL(&s_event_mux);
}

static bool mapping_is_available(const switch_mapping_t *mapping)
{
    if (mapping->is_gpio) {
        return mapping->gpio != GPIO_NUM_NC;
    }
    return mapping->expander < configured_expander_count() && mapping->pin < 8;
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

static void log_control_io_error(const char *source, esp_err_t err)
{
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_next_io_error_log) < 0) {
        return;
    }

    ESP_LOGW(TAG, "%s failed: %s", source, esp_err_to_name(err));
    s_next_io_error_log = now + pdMS_TO_TICKS(1000);
}

static esp_err_t init_i2c_peripherals(void *ctx)
{
    (void)ctx;

    ESP_RETURN_ON_ERROR(probe_i2c_addr(ESP_POCKET_AUDIO_ADS1115_ADDR), TAG, "ADS1115 not detected");
    ESP_RETURN_ON_ERROR(ads1115_init(&s_ads1115, s_i2c_bus, ESP_POCKET_AUDIO_ADS1115_ADDR, ESP_POCKET_AUDIO_CONTROL_I2C_CLK_HZ, ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS), TAG, "ADS1115 init failed");

    for (size_t i = 0; i < configured_expander_count(); ++i) {
        ESP_RETURN_ON_ERROR(probe_i2c_addr(PCF8574_ADDRS[i]), TAG, "PCF8574 not detected");
        ESP_RETURN_ON_ERROR(pcf8574_init(&s_pcf8574[i], s_i2c_bus, PCF8574_ADDRS[i], ESP_POCKET_AUDIO_CONTROL_I2C_CLK_HZ, ESP_POCKET_AUDIO_CONTROL_I2C_TIMEOUT_MS), TAG, "PCF8574 init failed");
        s_pcf_port_state[i] = 0xFF;
    }

    return ESP_OK;
}

static void debounce_switch(control_switch_t sw, bool pressed, TickType_t now_ticks)
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
        ESP_LOGV(TAG, "%s %s", controls_switch_name(sw), pressed ? "pressed" : "released");
    }

    if (pressed == debounce->stable_pressed && SWITCH_MAP[sw].is_gpio) {
        s_gpio_debounce_pending_mask &= ~(1UL << sw);
    }
}

static esp_err_t update_pcf_switches(TickType_t now_ticks)
{
    for (size_t i = 0; i < configured_expander_count(); ++i) {
        esp_err_t err = pcf8574_read_port(&s_pcf8574[i], &s_pcf_port_state[i]);
        if (err != ESP_OK) {
            log_control_io_error("PCF8574 read", err);
            return err;
        }
    }

    for (size_t i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
        if (!SWITCH_MAP[i].is_gpio) {
            debounce_switch((control_switch_t)i, raw_switch_pressed(&SWITCH_MAP[i]), now_ticks);
        }
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
        if (sw < CONTROL_SWITCH_COUNT && SWITCH_MAP[sw].is_gpio) {
            debounce_switch((control_switch_t)sw, raw_switch_pressed(&SWITCH_MAP[sw]), now_ticks);
        }
    }
}

static esp_err_t update_joystick(void)
{
    int16_t raw_x = 0;
    int16_t raw_y = 0;

    esp_err_t err = ads1115_read_single_ended(&s_ads1115, (ads1115_channel_t)ESP_POCKET_AUDIO_ADS1115_JOYSTICK_X_CH, &raw_x);
    if (err != ESP_OK) {
        log_control_io_error("ADS1115 X read", err);
        return err;
    }
    err = ads1115_read_single_ended(&s_ads1115, (ads1115_channel_t)ESP_POCKET_AUDIO_ADS1115_JOYSTICK_Y_CH, &raw_y);
    if (err != ESP_OK) {
        log_control_io_error("ADS1115 Y read", err);
        return err;
    }

    s_joystick.x.raw = raw_x;
    s_joystick.x.value = joystick_calibration_normalize(JOYSTICK_CAL_AXIS_X, raw_x);
    s_joystick.y.raw = raw_y;
    s_joystick.y.value = joystick_calibration_normalize(JOYSTICK_CAL_AXIS_Y, raw_y);
    s_joystick.direction = controls_joystick_direction_from_axes(s_joystick.x.value, s_joystick.y.value);
    return ESP_OK;
}

static float clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

#if ESP_POCKET_AUDIO_WHEEL_ENABLED
static float normalize_wheel_raw(int16_t raw)
{
    int32_t min_raw = ESP_POCKET_AUDIO_WHEEL_MIN_RAW;
    int32_t max_raw = ESP_POCKET_AUDIO_WHEEL_MAX_RAW;
    if (max_raw <= min_raw) {
        return 0.0f;
    }

    float value = ((float)raw - (float)min_raw) / (float)(max_raw - min_raw);
    value = clamp01(value);
#if ESP_POCKET_AUDIO_WHEEL_INVERT
    value = 1.0f - value;
#endif
    return value;
}
#endif

static void set_wheel_state(int16_t raw, float value, bool present)
{
    value = clamp01(value);
    s_wheel.present = present;
    s_wheel.raw = raw;
    s_wheel.value = value;
    s_wheel.percent = (uint8_t)((value * 100.0f) + 0.5f);
}

static esp_err_t update_wheel(void)
{
#if ESP_POCKET_AUDIO_WHEEL_ENABLED
    int16_t raw = 0;
    esp_err_t err = ads1115_read_single_ended(&s_ads1115, (ads1115_channel_t)ESP_POCKET_AUDIO_ADS1115_WHEEL_CH, &raw);
    if (err != ESP_OK) {
        log_control_io_error("ADS1115 wheel read", err);
        return err;
    }

    set_wheel_state(raw, normalize_wheel_raw(raw), true);
#else
    set_wheel_state(0, (float)ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT / 100.0f, false);
#endif
    return ESP_OK;
}

typedef struct {
    TickType_t now;
    esp_err_t switch_err;
    esp_err_t joystick_err;
    esp_err_t wheel_err;
} controls_i2c_update_t;

static esp_err_t update_i2c_inputs(void *ctx)
{
    controls_i2c_update_t *update = (controls_i2c_update_t *)ctx;
    if (update == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    update->switch_err = update_pcf_switches(update->now);
    update->joystick_err = update_joystick();
    update->wheel_err = update_wheel();
    if (update->switch_err != ESP_OK) {
        return update->switch_err;
    }
    if (update->joystick_err != ESP_OK) {
        return update->joystick_err;
    }
    return update->wheel_err;
}

static void log_pressed_switches(uint32_t pressed_mask)
{
    char line[128];
    size_t used = snprintf(line, sizeof(line), "pressed:");
    for (size_t i = 0; i < CONTROL_SWITCH_COUNT && used < sizeof(line); ++i) {
        if ((pressed_mask & (1UL << i)) != 0) {
            used += snprintf(line + used, sizeof(line) - used, " %s", controls_switch_name((control_switch_t)i));
        }
    }
    ESP_LOGV(TAG, "%s", line);
}

static void service_task(void *arg)
{
    (void)arg;
    TickType_t next_held_log = 0;
    control_joystick_direction_t last_direction = CONTROL_JOYSTICK_CENTER;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        controls_update();

        control_joystick_state_t joystick = {0};
        if (controls_joystick_read(&joystick) == ESP_OK && joystick.direction != last_direction) {
            ESP_LOGV(TAG, "joystick %s x=%d %.3f y=%d %.3f",
                     controls_joystick_direction_name(joystick.direction),
                     joystick.x.raw,
                     joystick.x.value,
                     joystick.y.raw,
                     joystick.y.value);
            last_direction = joystick.direction;
        }

        uint32_t pressed_mask = controls_pressed_mask();
        if (pressed_mask != 0 && (int32_t)(now - next_held_log) >= 0) {
            log_pressed_switches(pressed_mask);
            next_held_log = now + HELD_LOG_TICKS;
        }

        vTaskDelay(SERVICE_PERIOD_TICKS);
    }
}

static esp_err_t start_service_task(void)
{
    if (s_service_task != NULL) {
        return ESP_OK;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        service_task,
        "controls",
        CONTROLS_TASK_STACK_SIZE,
        NULL,
        4,
        &s_service_task,
        0
    );
    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t controls_init(i2c_master_bus_handle_t bus)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_i2c_bus = bus;
    ESP_RETURN_ON_ERROR(joystick_calibration_init(), TAG, "joystick calibration init failed");
    ESP_RETURN_ON_ERROR(init_interrupt_pin(), TAG, "PCF interrupt pin failed");
    ESP_RETURN_ON_ERROR(control_i2c_call(init_i2c_peripherals, NULL, pdMS_TO_TICKS(500)), TAG, "control I2C peripheral init failed");

    ESP_RETURN_ON_ERROR(init_gpio_switches(), TAG, "GPIO switch init failed");

    TickType_t now = xTaskGetTickCount();
    for (size_t i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
        bool pressed = SWITCH_MAP[i].is_gpio ? raw_switch_pressed(&SWITCH_MAP[i]) : false;
        s_switches[i].stable_pressed = pressed;
        s_switches[i].sampled_pressed = pressed;
        s_switches[i].changed_at = now;
    }

    ESP_RETURN_ON_ERROR(init_gpio_interrupts(), TAG, "GPIO interrupt init failed");
    s_event_head = 0;
    s_event_tail = 0;
    s_initialized = true;
    ESP_RETURN_ON_ERROR(controls_update(), TAG, "initial controls update failed");
    ESP_RETURN_ON_ERROR(start_service_task(), TAG, "controls task failed");

    ESP_LOGI(TAG, "controls initialized");
    return ESP_OK;
}

esp_err_t controls_update(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t now = xTaskGetTickCount();
    controls_i2c_update_t update = {
        .now = now,
        .switch_err = ESP_FAIL,
        .joystick_err = ESP_FAIL,
        .wheel_err = ESP_FAIL,
    };
    esp_err_t i2c_err = control_i2c_call(update_i2c_inputs, &update, pdMS_TO_TICKS(10));
    if (i2c_err == ESP_ERR_TIMEOUT) {
        log_control_io_error("control I2C queue", i2c_err);
    }
    update_gpio_switches(now);
    return i2c_err;
}

bool controls_expander_interrupt_asserted(void)
{
    if (ESP_POCKET_AUDIO_PCF8574_INT_GPIO == GPIO_NUM_NC) {
        return false;
    }
    return gpio_get_level(ESP_POCKET_AUDIO_PCF8574_INT_GPIO) == 0;
}

gpio_num_t controls_expander_interrupt_gpio(void)
{
    return ESP_POCKET_AUDIO_PCF8574_INT_GPIO;
}

bool controls_switch_is_pressed(control_switch_t sw)
{
    return sw < CONTROL_SWITCH_COUNT ? s_switches[sw].stable_pressed : false;
}

uint32_t controls_pressed_mask(void)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
        if (s_switches[i].stable_pressed) {
            mask |= 1UL << i;
        }
    }
    return mask;
}

esp_err_t controls_pop_event(control_event_t *event)
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

esp_err_t controls_axis_read(control_axis_t axis, control_axis_state_t *state)
{
    if (state == NULL || axis >= CONTROL_AXIS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = axis == CONTROL_AXIS_X ? s_joystick.x : s_joystick.y;
    return ESP_OK;
}

esp_err_t controls_joystick_read(control_joystick_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_joystick;
    return ESP_OK;
}

esp_err_t controls_wheel_read(control_wheel_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_wheel;
    return ESP_OK;
}
