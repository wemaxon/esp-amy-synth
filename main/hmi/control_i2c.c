#include "hmi/control_i2c.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CONTROL_I2C_QUEUE_LEN 8
#define CONTROL_I2C_TASK_STACK_SIZE 4096
#define CONTROL_I2C_TASK_PRIORITY 6
#define CONTROL_I2C_TASK_CORE 0

typedef struct {
    control_i2c_op_t op;
    void *ctx;
    SemaphoreHandle_t done;
    esp_err_t *result;
} control_i2c_request_t;

static const char *TAG = "CONTROL_I2C";

static i2c_master_bus_handle_t s_bus;
static QueueHandle_t s_queue_high;
static QueueHandle_t s_queue_low;
static QueueSetHandle_t s_queue_set;
static TaskHandle_t s_task;

static bool receive_next_request(control_i2c_request_t *request)
{
    if (xQueueSelectFromSet(s_queue_set, portMAX_DELAY) == NULL) {
        return false;
    }

    if (xQueueReceive(s_queue_high, request, 0) == pdTRUE) {
        return true;
    }
    return xQueueReceive(s_queue_low, request, 0) == pdTRUE;
}

static void control_i2c_task(void *arg)
{
    (void)arg;

    while (true) {
        control_i2c_request_t request = {0};
        if (!receive_next_request(&request)) {
            continue;
        }

        esp_err_t result = request.op != NULL ? request.op(request.ctx) : ESP_ERR_INVALID_ARG;
        if (request.result != NULL) {
            *request.result = result;
        }
        if (request.done != NULL) {
            xSemaphoreGive(request.done);
        }
    }
}

esp_err_t control_i2c_service_start(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_bus = bus;
    s_queue_high = xQueueCreate(CONTROL_I2C_QUEUE_LEN, sizeof(control_i2c_request_t));
    s_queue_low = xQueueCreate(CONTROL_I2C_QUEUE_LEN, sizeof(control_i2c_request_t));
    s_queue_set = xQueueCreateSet(CONTROL_I2C_QUEUE_LEN * 2);
    if (s_queue_high == NULL || s_queue_low == NULL || s_queue_set == NULL) {
        if (s_queue_high != NULL) {
            vQueueDelete(s_queue_high);
        }
        if (s_queue_low != NULL) {
            vQueueDelete(s_queue_low);
        }
        if (s_queue_set != NULL) {
            vQueueDelete(s_queue_set);
        }
        s_queue_high = NULL;
        s_queue_low = NULL;
        s_queue_set = NULL;
        return ESP_ERR_NO_MEM;
    }
    xQueueAddToSet(s_queue_high, s_queue_set);
    xQueueAddToSet(s_queue_low, s_queue_set);

    BaseType_t task_created = xTaskCreatePinnedToCore(
        control_i2c_task,
        "control_i2c",
        CONTROL_I2C_TASK_STACK_SIZE,
        NULL,
        CONTROL_I2C_TASK_PRIORITY,
        &s_task,
        CONTROL_I2C_TASK_CORE
    );
    if (task_created != pdPASS) {
        vQueueDelete(s_queue_high);
        vQueueDelete(s_queue_low);
        vQueueDelete(s_queue_set);
        s_queue_high = NULL;
        s_queue_low = NULL;
        s_queue_set = NULL;
        s_bus = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "control I2C service started");
    return ESP_OK;
}

i2c_master_bus_handle_t control_i2c_bus(void)
{
    return s_bus;
}

esp_err_t control_i2c_call(control_i2c_op_t op, void *ctx, TickType_t queue_timeout_ticks)
{
    return control_i2c_call_priority(CONTROL_I2C_PRIORITY_HIGH, op, ctx, queue_timeout_ticks);
}

esp_err_t control_i2c_call_priority(
    control_i2c_priority_t priority,
    control_i2c_op_t op,
    void *ctx,
    TickType_t queue_timeout_ticks
)
{
    if (op == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_queue_high == NULL || s_queue_low == NULL || s_queue_set == NULL || s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskGetCurrentTaskHandle() == s_task) {
        return op(ctx);
    }

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_FAIL;
    control_i2c_request_t request = {
        .op = op,
        .ctx = ctx,
        .done = done,
        .result = &result,
    };

    QueueHandle_t queue = priority == CONTROL_I2C_PRIORITY_LOW ? s_queue_low : s_queue_high;
    if (xQueueSend(queue, &request, queue_timeout_ticks) != pdTRUE) {
        vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return result;
}
