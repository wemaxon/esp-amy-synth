#ifndef SYNTH_APP_UI_MODEL_H
#define SYNTH_APP_UI_MODEL_H

#include "hmi/play_ui.h"
#include "synth/app/state.h"

#ifdef __cplusplus
extern "C" {
#endif

void synth_app_ui_render(const synth_app_state_t *state, play_ui_screen_t screen);

#ifdef __cplusplus
}
#endif

#endif

