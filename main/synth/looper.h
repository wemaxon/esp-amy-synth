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
/* Track 1 length in bars at power-up; 0 would be free length. */
#define SYNTH_LOOPER_DEFAULT_BARS 4
#define SYNTH_LOOPER_MAX_SECONDS 20

typedef enum {
    SYNTH_LOOPER_TRACK_EMPTY = 0,
    SYNTH_LOOPER_TRACK_WAITING,
    SYNTH_LOOPER_TRACK_RECORDING,
    SYNTH_LOOPER_TRACK_LOOPING,
    SYNTH_LOOPER_TRACK_OFF,
    SYNTH_LOOPER_TRACK_STATE_COUNT,
} synth_looper_track_state_t;

typedef struct {
    uint8_t live_peak_percent;
    uint8_t loop_peak_percent;
    uint8_t output_peak_percent;
    uint16_t clipped_samples;
} synth_looper_meter_t;

typedef struct {
    synth_looper_track_state_t track_state[SYNTH_LOOPER_TRACK_COUNT];
    uint32_t track_ms[SYNTH_LOOPER_TRACK_COUNT];
    uint32_t track_position_ms[SYNTH_LOOPER_TRACK_COUNT];
    uint8_t track_volume_percent[SYNTH_LOOPER_TRACK_COUNT];
    bool track_muted[SYNTH_LOOPER_TRACK_COUNT];
    bool track_soloed[SYNTH_LOOPER_TRACK_COUNT];
    synth_looper_meter_t meter;
    uint8_t current_track;
    uint8_t page;
    uint8_t bar_count;
    bool paused;
    bool metronome_enabled;
    bool record_metronome_enabled;
    bool record_metronome_active;
    bool solo_active;
    uint32_t master_ms;
    uint32_t progress_ms;
    uint32_t progress_total_ms;
    uint32_t wait_ms;
    uint32_t max_seconds;
    bool current_track_armed;
    bool waiting_for_sync;
    char message[22];
} synth_looper_snapshot_t;

typedef struct {
    uint32_t command_queue_high_water;
    uint32_t command_rejected;
    uint32_t command_depth;
    uint32_t max_command_latency_blocks;
    uint32_t snapshot_reader_retries;
    uint32_t snapshot_publish_skipped;
    uint32_t max_process_us;
    uint32_t process_over_budget;
} synth_looper_diagnostics_t;

esp_err_t synth_looper_init(uint32_t sample_rate_hz, uint8_t channel_count);
/* Mixes loop playback into block. record_block is what takes capture (the
 * synth bus without drums); it must have the same layout as block. */
void synth_looper_process_audio(int16_t *block, const int16_t *record_block, uint32_t frames, uint8_t channels);

/* Control APIs are single-producer and apply accepted commands next audio block. */
/* BPM must be nonzero; invalid values are rejected rather than substituted. */
esp_err_t synth_looper_click(uint16_t bpm);
/* A running beat's grid: anchor_frame is the audio frame of its step 0.
 * Recordings armed while it runs start on its bar (bar mode) or beat (free
 * length), so loops and the live beat share one grid. */
esp_err_t synth_looper_set_beat_grid(bool running, uint64_t anchor_frame, uint16_t bpm);
esp_err_t synth_looper_select_track(int8_t steps);
esp_err_t synth_looper_set_track_volume(uint8_t track, uint8_t percent);
esp_err_t synth_looper_set_current_track_volume(uint8_t percent);
esp_err_t synth_looper_toggle_track_mute(uint8_t track);
esp_err_t synth_looper_toggle_track_solo(uint8_t track);
esp_err_t synth_looper_toggle_metronome(uint16_t bpm);
esp_err_t synth_looper_toggle_record_metronome(uint16_t bpm);
esp_err_t synth_looper_set_metronome_bpm(uint16_t bpm);
esp_err_t synth_looper_adjust_bars(int8_t steps);
esp_err_t synth_looper_toggle_pause(void);
esp_err_t synth_looper_clear_all(void);
esp_err_t synth_looper_clear_track(uint8_t track);
bool synth_looper_current_track_is_waiting(void);
/* True during a count-in, a sync wait or a recording: the player's hands are
 * on the chords, so the joystick must not navigate the looper. */
bool synth_looper_take_in_progress(void);

/* Snapshots are coherent copies and never make the audio task wait. */
void synth_looper_get_snapshot(synth_looper_snapshot_t *snapshot);

/* The audio-frame clock shared by loops and the live beat. frame is at most
 * one audio block old. loop_anchor_frame is position 0 of the first loop. */
typedef struct {
    uint32_t sample_rate_hz;
    uint64_t frame;
    bool has_loop;
    uint64_t loop_anchor_frame;
    uint32_t loop_length_frames;
} synth_looper_clock_t;

bool synth_looper_get_clock(synth_looper_clock_t *clock);
/* Audio task only: the frame index of the next block to be processed. */
uint64_t synth_looper_next_block_frame(void);
void synth_looper_get_diagnostics(synth_looper_diagnostics_t *diagnostics);
void synth_looper_record_process_time(uint32_t process_us, uint32_t audio_block_budget_us);
const char *synth_looper_track_state_name(synth_looper_track_state_t state);
const char *synth_looper_track_state_short_name(synth_looper_track_state_t state);

#ifdef __cplusplus
}
#endif

#endif
