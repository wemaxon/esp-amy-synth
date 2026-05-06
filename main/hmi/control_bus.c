#include "hmi/control_bus.h"

#include "esp_err.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_control_bus_mutex;

esp_err_t control_bus_lock_init(void)
{
    if (s_control_bus_mutex != NULL) {
        return ESP_OK;
    }

    s_control_bus_mutex = xSemaphoreCreateMutex();
    return s_control_bus_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t control_bus_lock(TickType_t timeout_ticks)
{
    esp_err_t err = control_bus_lock_init();
    if (err != ESP_OK) {
        return err;
    }

    return xSemaphoreTake(s_control_bus_mutex, timeout_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void control_bus_unlock(void)
{
    if (s_control_bus_mutex != NULL) {
        xSemaphoreGive(s_control_bus_mutex);
    }
}
