#ifndef SYNTH_APP_MIC_H
#define SYNTH_APP_MIC_H

#include "synth/mic_sample.h"

/* Finalize a captured sample on the application task, never the audio task. */
esp_err_t synth_app_mic_finalize(synth_mic_sample_snapshot_t *snapshot);

#endif
