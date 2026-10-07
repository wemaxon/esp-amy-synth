#include "hil/hil_app.h"

#include <stdlib.h>
#include <string.h>

#include "hil_console.h"
#include "hmi/controls.h"

static bool parse_switch(const char *name, control_switch_t *sw)
{
    for (int i = 0; i < CONTROL_SWITCH_COUNT; ++i) {
        if (strcmp(name, controls_switch_short_name((control_switch_t)i)) == 0) {
            *sw = (control_switch_t)i;
            return true;
        }
    }
    return false;
}

static void set_switch(int argc, char **argv, bool pressed)
{
    control_switch_t sw;
    if (argc != 2 || !parse_switch(argv[1], &sw)) {
        hil_reply_error("usage: press|release <1-7|J|K|M|S>");
        return;
    }
    controls_set_virtual_switch(sw, pressed);
    hil_reply_ok();
}

void hil_command_press(int argc, char **argv)
{
    set_switch(argc, argv, true);
}

void hil_command_release(int argc, char **argv)
{
    set_switch(argc, argv, false);
}

void hil_command_joy(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        controls_set_virtual_joystick(false, 0.0f, 0.0f);
    } else if (argc == 3) {
        controls_set_virtual_joystick(true, strtof(argv[1], NULL), strtof(argv[2], NULL));
    } else {
        hil_reply_error("usage: joy <x> <y> | joy off");
        return;
    }
    hil_reply_ok();
}
