#ifndef SYNTH_LOOPER_H
#define SYNTH_LOOPER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_LOOPER_TRACK_COUNT 6
#define SYNTH_LOOPER_TRACKS_PER_PAGE 2
#define SYNTH_LOOPER_MAX_BARS 8

#ifndef SYNTH_LOOPER_MAX_SECONDS
#define SYNTH_LOOPER_MAX_SECONDS 20
#endif

typedef enum {
    SYNTH_LOOPER_TRACK_EMPTY = 0,
    SYNTH_LOOPER_TRACK_WAITING,
    SYNTH_LOOPER_TRACK_RECORDING,
    SYNTH_LOOPER_TRACK_LOOPING,
    SYNTH_LOOPER_TRACK_OFF,
    SYNTH_LOOPER_TRACK_STATE_COUNT,
} synth_looper_track_state_t;

typedef struct {
    synth_looper_track_state_t track_state[SYNTH_LOOPER_TRACK_COUNT];
    uint32_t track_ms[SYNTH_LOOPER_TRACK_COUNT];
    uint8_t current_track;
    uint8_t page;
    uint8_t bar_count;
    bool paused;
    uint32_t master_ms;
    uint32_t max_seconds;
    char message[22];
} synth_looper_snapshot_t;

esp_err_t synth_looper_init(uint32_t sample_rate_hz, uint8_t channel_count);
void synth_looper_process_audio(int16_t *block, uint32_t frames, uint8_t channels);

esp_err_t synth_looper_click(uint16_t bpm);
esp_err_t synth_looper_select_track(int8_t steps);
esp_err_t synth_looper_adjust_bars(int8_t steps);
esp_err_t synth_looper_toggle_pause(void);
bool synth_looper_current_track_is_waiting(void);

void synth_looper_get_snapshot(synth_looper_snapshot_t *snapshot);
const char *synth_looper_track_state_name(synth_looper_track_state_t state);
const char *synth_looper_track_state_short_name(synth_looper_track_state_t state);

#ifdef __cplusplus
}
#endif

#endif
