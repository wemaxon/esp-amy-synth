#include "hil_console.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HIL_CONSOLE";

#define LINE_SIZE 128U
#define MAX_ARGS 8U
/* Raw bytes per data line; a multiple of 3 so lines encode independently. */
#define DATA_CHUNK 3072U

static char s_encoded[(DATA_CHUNK / 3U) * 4U + 1U];

void hil_reply_ok(void)
{
    printf("@{\"ok\":true}\n");
    fflush(stdout);
}

void hil_reply_json(const hil_json_t *json)
{
    if (!hil_json_complete(json)) {
        hil_reply_error("reply too large");
        return;
    }
    printf("@%s\n", json->out);
    fflush(stdout);
}

void hil_reply_error(const char *message)
{
    char text[160];
    hil_json_t json;
    hil_json_init(&json, text, sizeof(text));
    hil_json_object(&json, NULL);
    hil_json_string(&json, "error", message);
    hil_json_end(&json);
    printf("@%s\n", hil_json_complete(&json) ? text : "{\"error\":\"error message too long\"}");
    fflush(stdout);
}

static size_t base64_encode(const uint8_t *data, size_t length, char *out)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t used = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t group = (uint32_t)data[i] << 16;
        if (i + 1 < length) {
            group |= (uint32_t)data[i + 1] << 8;
        }
        if (i + 2 < length) {
            group |= data[i + 2];
        }
        out[used++] = ALPHABET[(group >> 18) & 0x3F];
        out[used++] = ALPHABET[(group >> 12) & 0x3F];
        out[used++] = i + 1 < length ? ALPHABET[(group >> 6) & 0x3F] : '=';
        out[used++] = i + 2 < length ? ALPHABET[group & 0x3F] : '=';
    }
    out[used] = '\0';
    return used;
}

void hil_send_data(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t offset = 0; offset < length; offset += DATA_CHUNK) {
        size_t chunk = length - offset < DATA_CHUNK ? length - offset : DATA_CHUNK;
        base64_encode(bytes + offset, chunk, s_encoded);
        printf("@=%s\n", s_encoded);
    }
    fflush(stdout);
}

/* esp_console passes each command's own table entry back as its context. */
static int run_command(void *context, int argc, char **argv)
{
    ((const hil_command_t *)context)->run(argc, argv);
    return 0;
}

static void command_ping(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    hil_reply_ok();
}

static void command_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    hil_reply_ok();
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

static const hil_command_t BUILT_IN[] = {
    {"ping", "ping", command_ping},
    {"reboot", "reboot", command_reboot},
};

static esp_err_t register_commands(const hil_command_t *commands, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const esp_console_cmd_t command = {
            .command = commands[i].name,
            .help = commands[i].usage,
            .func_w_context = run_command,
            .context = (void *)&commands[i],
        };
        ESP_RETURN_ON_ERROR(esp_console_cmd_register(&command), TAG, "register %s failed", commands[i].name);
    }
    return ESP_OK;
}

static void console_task(void *arg)
{
    (void)arg;
    char line[LINE_SIZE];
    printf("@{\"ready\":true}\n");
    fflush(stdout);
    while (true) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0'; /* esp_console splits on spaces only */
        int ignored = 0;
        esp_err_t err = esp_console_run(line, &ignored);
        if (err == ESP_ERR_NOT_FOUND) {
            hil_reply_error("unknown command");
        }
    }
}

esp_err_t hil_console_start(const hil_command_t *commands, size_t count)
{
    const esp_console_config_t config = {
        .max_cmdline_length = LINE_SIZE,
        .max_cmdline_args = MAX_ARGS,
    };
    ESP_RETURN_ON_ERROR(esp_console_init(&config), TAG, "console init failed");
    ESP_RETURN_ON_ERROR(register_commands(BUILT_IN, sizeof(BUILT_IN) / sizeof(BUILT_IN[0])),
                        TAG,
                        "built-in command registration failed");
    ESP_RETURN_ON_ERROR(register_commands(commands, count), TAG, "command registration failed");

    usb_serial_jtag_driver_config_t driver = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&driver), TAG, "USB serial driver install failed");
    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);

    BaseType_t created = xTaskCreatePinnedToCore(console_task, "hil_console", 6144, NULL, 3, NULL, 0);
    return created == pdPASS ? ESP_OK : ESP_FAIL;
}
