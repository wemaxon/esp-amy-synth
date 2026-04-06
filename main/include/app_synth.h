#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "audio_element.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_synth_init(void);

audio_element_handle_t app_synth_create_stream(int task_core, int task_prio);

int app_synth_sample_rate(void);

int app_synth_channels(void);

bool app_synth_handle_button_event(const audio_event_iface_msg_t *msg);

void app_synth_update(uint32_t now_ticks);

#ifdef __cplusplus
}
#endif
