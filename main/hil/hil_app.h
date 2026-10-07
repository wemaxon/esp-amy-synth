#ifndef HIL_APP_H
#define HIL_APP_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This firmware's commands for the hardware-in-the-loop console (built with
 * CONFIG_HIL_CONSOLE). They use the firmware's ports only: virtual input
 * (controls), the app snapshot, looper clock/snapshot and the audio tap.
 *
 *   press|release <1-7|J|K|M|S>  virtual switch (J click, K key, M mode, S sound)
 *   joy <x> <y> | joy off        virtual joystick, -1..1, up = +y
 *   state                        app + looper state
 *   capture <output|synth> <ms>  mono audio as data lines, then a header reply
 */
esp_err_t hil_app_start(void);

/* Command handlers (hil_console signature). */
void hil_command_press(int argc, char **argv);
void hil_command_release(int argc, char **argv);
void hil_command_joy(int argc, char **argv);
void hil_command_state(int argc, char **argv);
void hil_command_capture(int argc, char **argv);
esp_err_t hil_capture_init(void);

#ifdef __cplusplus
}
#endif

#endif
