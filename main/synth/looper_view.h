#ifndef SYNTH_LOOPER_VIEW_H
#define SYNTH_LOOPER_VIEW_H

#include "synth/looper_transport.h"

/* Called by snapshot readers, outside the audio callback. */
void synth_looper_view_to_snapshot(uint32_t sample_rate_hz,
                                   const synth_looper_runtime_view_t *view,
                                   synth_looper_snapshot_t *snapshot);

#endif
