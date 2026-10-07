#include "hil/hil_app.h"

#include "esp_check.h"
#include "hil_console.h"

static const char *TAG = "HIL_APP";

static const hil_command_t COMMANDS[] = {
    {"press", "press <1-7|J|K|M|S>", hil_command_press},
    {"release", "release <1-7|J|K|M|S>", hil_command_release},
    {"joy", "joy <x> <y> | joy off", hil_command_joy},
    {"state", "state", hil_command_state},
    {"capture", "capture <output|synth> <ms>", hil_command_capture},
};

esp_err_t hil_app_start(void)
{
    ESP_RETURN_ON_ERROR(hil_capture_init(), TAG, "capture init failed");
    return hil_console_start(COMMANDS, sizeof(COMMANDS) / sizeof(COMMANDS[0]));
}
