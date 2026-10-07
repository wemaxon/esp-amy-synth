#ifndef HIL_CONSOLE_H
#define HIL_CONSOLE_H

#include <stddef.h>

#include "esp_err.h"
#include "hil_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hardware-in-the-loop console on the USB-Serial-JTAG console (enabled by
 * CONFIG_HIL_CONSOLE). One command per line; every command sends exactly one
 * reply line, '@' followed by JSON, so log lines can interleave. Bulk data is
 * sent before the reply as '@=' base64 lines.
 *
 * Built-in commands: ping, reboot. After start it sends {"ready":true}.
 */

/* A command handler: argv[0] is the command name. It must call exactly one
 * hil_reply_* function (after any hil_send_data). */
typedef void (*hil_command_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *usage;
    hil_command_fn run;
} hil_command_t;

/* commands must stay valid while the console runs (use a static table). */
esp_err_t hil_console_start(const hil_command_t *commands, size_t count);

void hil_reply_ok(void);
void hil_reply_error(const char *message);
/* Sends a complete JSON value; an incomplete one becomes an error reply. */
void hil_reply_json(const hil_json_t *json);
void hil_send_data(const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
