#include "synth/looper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "synth/looper_dsp.h"
#include "synth/looper_take.h"
#include "synth/looper_track.h"
#include "synth/looper_transport.h"
#include "synth/looper_view.h"
#include "synth/metronome.h"
#include "synth/transport_timing.h"

static const char *TAG = "SYNTH_LOOPER";

static const uint8_t LOOP_TRACK_DEFAULT_VOLUME_PERCENT = 100;
static const uint32_t MIN_RECORD_MS = 120;
/* Fixed record latency compensation: ~46 ms of I2S output buffering (8 x 256
 * frames) before the player hears the click, plus ~45 ms from button press to
 * sound (25 ms debounce, 20 ms control scan and app loop). Must exceed SEAM. */
static const uint32_t RECORD_LATENCY_MS = 90;
/* Loop-end crossfade into the pre-roll; keeps the wrap click-free. */
static const uint32_t SEAM_MS = 50;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static const uint32_t METER_LOG_INTERVAL_MS = 1000;
static const uint32_t CLIP_LOG_INTERVAL_MS = 500;
#endif
/* Initialized before task startup, then written only by the audio task. */
static synth_looper_track_t s_tracks[SYNTH_LOOPER_TRACK_COUNT];
static uint32_t s_sample_rate_hz;
static uint8_t s_channel_count;
static uint32_t s_capacity_frames;
static uint32_t s_master_length_frames;
static uint64_t s_global_frame;
static uint8_t s_current_track;
static uint8_t s_bar_count = SYNTH_LOOPER_DEFAULT_BARS;
/* At most one take records at a time; it may outlive the track's RECORDING
 * state by the latency, finishing the loop body after playback has begun. */
static synth_looper_take_t s_take;
static uint8_t s_take_track = UINT8_MAX;
static uint64_t s_take_anchor_frame;
static uint64_t s_master_anchor_frame;
/* Grid of the live drum beat, which plays on its own bus and is not recorded. */
static bool s_beat_grid_valid;
static uint64_t s_beat_anchor_frame;
static bool s_paused;
static bool s_initialized;
static uint16_t s_current_bpm = 120;
static synth_metronome_t s_global_metronome;
static synth_metronome_t s_record_metronome;
static bool s_record_metronome_enabled;
static uint8_t s_mute_mask;
static uint8_t s_solo_mask;
static synth_looper_message_code_t s_message_code = SYNTH_LOOPER_MESSAGE_EMPTY;
static uint8_t s_message_arg0;
static uint8_t s_message_arg1;
static _Atomic uint32_t s_audio_block;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static synth_looper_meter_t s_meter;
static uint64_t s_next_meter_log_frame;
static uint64_t s_next_clip_log_frame;
#endif
/* Application-to-audio commands and audio-to-application immutable views. */
static synth_looper_command_queue_t s_command_queue;
static synth_looper_snapshot_store_t s_snapshot_store;
static _Atomic uint32_t s_max_command_latency_blocks;
static _Atomic uint32_t s_max_process_us;
static _Atomic uint32_t s_process_over_budget;

static uint8_t track_bit(uint8_t track);

static uint32_t ms_to_frames(uint32_t ms)
{
    return (uint32_t)(((uint64_t)ms * s_sample_rate_hz) / 1000ULL);
}

static uint32_t bars_to_frames(uint8_t bars, uint16_t bpm)
{
    return synth_transport_bar_span(s_sample_rate_hz, bpm, bars);
}

static void set_current_bpm(uint16_t bpm)
{
    s_current_bpm = bpm;
}

static size_t track_buffer_bytes(void)
{
    return (size_t)s_capacity_frames * sizeof(int16_t);
}

static void set_message(synth_looper_message_code_t code, uint8_t arg0, uint8_t arg1)
{
    s_message_code = code;
    s_message_arg0 = arg0;
    s_message_arg1 = arg1;
}

static bool has_any_loop(void)
{
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        if (s_tracks[i].state == SYNTH_LOOPER_TRACK_LOOPING) {
            return true;
        }
    }
    return false;
}

static bool record_metronome_should_run_for_track(synth_looper_track_state_t state,
                                                  bool record_armed,
                                                  bool sync_to_master,
                                                  uint32_t count_in_beat_frames)
{
    if (state == SYNTH_LOOPER_TRACK_RECORDING) {
        return true;
    }
    return state == SYNTH_LOOPER_TRACK_WAITING &&
           record_armed &&
           !sync_to_master &&
           count_in_beat_frames > 0;
}

static bool record_metronome_should_run(void)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        if (record_metronome_should_run_for_track(s_tracks[track].state,
                                                  s_tracks[track].record_armed,
                                                  s_tracks[track].sync_to_master,
                                                  s_tracks[track].count_in_beat_frames)) {
            return true;
        }
    }
    return false;
}

static void clear_track(uint8_t track)
{
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return;
    }

    if (s_take_track == track) {
        synth_looper_take_abort(&s_take);
        s_take_track = UINT8_MAX;
    }
    synth_looper_track_reset(&s_tracks[track], LOOP_TRACK_DEFAULT_VOLUME_PERCENT);
    s_mute_mask &= (uint8_t)~track_bit(track);
    s_solo_mask &= (uint8_t)~track_bit(track);
}

static void clear_all(void)
{
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        clear_track(i);
    }
    s_master_length_frames = 0;
    s_paused = false;
    s_current_track = 0;
    s_mute_mask = 0;
    s_solo_mask = 0;
    synth_metronome_set_enabled(&s_record_metronome, false, s_current_bpm, s_global_frame);
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    memset(&s_meter, 0, sizeof(s_meter));
    s_next_meter_log_frame = 0;
    s_next_clip_log_frame = 0;
#endif
    set_message(SYNTH_LOOPER_MESSAGE_CLEARED, 0, 0);
}

static uint8_t next_empty_track(uint8_t from_track)
{
    for (uint8_t offset = 1; offset <= SYNTH_LOOPER_TRACK_COUNT; ++offset) {
        uint8_t track = (uint8_t)((from_track + offset) % SYNTH_LOOPER_TRACK_COUNT);
        if (s_tracks[track].state == SYNTH_LOOPER_TRACK_EMPTY) {
            return track;
        }
    }
    return from_track;
}

static uint8_t track_bit(uint8_t track)
{
    return (uint8_t)(1U << track);
}

static bool track_is_audible(uint8_t track)
{
    uint8_t bit = track_bit(track);
    if (s_solo_mask != 0) {
        return (s_solo_mask & bit) != 0;
    }
    return (s_mute_mask & bit) == 0;
}

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static void log_meter_if_due(const synth_looper_meter_t *meter,
                             uint64_t block_start_frame,
                             uint8_t max_audible_tracks)
{
    if (meter == NULL || s_sample_rate_hz == 0) {
        return;
    }

    bool clipped = meter->clipped_samples > 0;
    uint64_t *next_log_frame = clipped ? &s_next_clip_log_frame : &s_next_meter_log_frame;
    if (block_start_frame < *next_log_frame) {
        return;
    }

    uint32_t interval_frames = ms_to_frames(clipped ? CLIP_LOG_INTERVAL_MS : METER_LOG_INTERVAL_MS);
    if (interval_frames == 0) {
        interval_frames = 1;
    }
    *next_log_frame = block_start_frame + interval_frames;

    if (clipped) {
        ESP_LOGW(TAG,
                 "mix clipped live=%u%% loop=%u%% out=%u%% clipped=%u audible_tracks=%u",
                 (unsigned)meter->live_peak_percent,
                 (unsigned)meter->loop_peak_percent,
                 (unsigned)meter->output_peak_percent,
                 (unsigned)meter->clipped_samples,
                 (unsigned)max_audible_tracks);
    } else {
        ESP_LOGD(TAG,
                 "mix meter live=%u%% loop=%u%% out=%u%% clipped=%u audible_tracks=%u",
                 (unsigned)meter->live_peak_percent,
                 (unsigned)meter->loop_peak_percent,
                 (unsigned)meter->output_peak_percent,
                 (unsigned)meter->clipped_samples,
                 (unsigned)max_audible_tracks);
    }
}
#endif

static esp_err_t ensure_track_buffer(uint8_t track)
{
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tracks[track].samples != NULL) {
        return ESP_OK;
    }

    size_t buffer_bytes = track_buffer_bytes();
    int16_t *buffer = heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        set_message(SYNTH_LOOPER_MESSAGE_NO_MEMORY, 0, 0);
        ESP_LOGW(TAG,
                 "failed to allocate %lu byte looper track buffer",
                 (unsigned long)buffer_bytes);
        return ESP_ERR_NO_MEM;
    }
    memset(buffer, 0, buffer_bytes);

    s_tracks[track].samples = buffer;
    return ESP_OK;
}

static esp_err_t allocate_all_track_buffers(void)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        ESP_RETURN_ON_ERROR(ensure_track_buffer(track), TAG, "looper track buffer allocation failed");
    }
    return ESP_OK;
}

static void free_track_buffers(void)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        heap_caps_free(s_tracks[track].samples);
        s_tracks[track].samples = NULL;
    }
}

static void begin_take(uint8_t track, uint32_t length_frames, uint64_t anchor_frame)
{
    synth_looper_take_begin(&s_take, s_tracks[track].samples, s_capacity_frames, length_frames);
    s_take_track = track;
    s_take_anchor_frame = anchor_frame;
}

/* First line of the beat grid (every sixteenths_per_line steps from its
 * anchor) at or after earliest; uses the drum transport's rational timing. */
static uint64_t next_beat_grid_line(uint64_t earliest, uint32_t sixteenths_per_line)
{
    if (earliest <= s_beat_anchor_frame) {
        return s_beat_anchor_frame;
    }
    uint64_t line_frames = synth_transport_step_offset(s_sample_rate_hz, s_current_bpm, sixteenths_per_line, 4);
    if (line_frames == 0) {
        return earliest;
    }
    /* line_frames is rounded down; settle on the exact line in both directions. */
    uint32_t line = (uint32_t)((earliest - s_beat_anchor_frame) / line_frames);
    while (line > 0 &&
           s_beat_anchor_frame +
                   synth_transport_step_offset(s_sample_rate_hz, s_current_bpm, (line - 1U) * sixteenths_per_line, 4) >=
               earliest) {
        --line;
    }
    while (s_beat_anchor_frame +
               synth_transport_step_offset(s_sample_rate_hz, s_current_bpm, line * sixteenths_per_line, 4) <
           earliest) {
        ++line;
    }
    return s_beat_anchor_frame +
           synth_transport_step_offset(s_sample_rate_hz, s_current_bpm, line * sixteenths_per_line, 4);
}

static void start_recording(uint8_t track, uint32_t target_frames)
{
    synth_looper_track_start_recording(&s_tracks[track], target_frames);
    begin_take(track, target_frames, s_global_frame);
    if (s_record_metronome_enabled) {
        synth_metronome_set_enabled(&s_record_metronome, true, s_current_bpm, s_global_frame);
    }

    set_message(SYNTH_LOOPER_MESSAGE_RECORDING, 0, 0);
}

/* The first loop is whole bars in bar mode and whole beats in free length. */
static uint32_t length_unit_frames(void)
{
    if (s_bar_count > 0) {
        return bars_to_frames(1, s_current_bpm);
    }
    return synth_metronome_beat_frames(s_sample_rate_hz, s_current_bpm);
}

static void set_take_length(uint8_t track, uint32_t length)
{
    if (s_take_track == track &&
        synth_looper_take_set_length(&s_take, length) == SYNTH_LOOPER_TAKE_DONE) {
        s_take_track = UINT8_MAX;
    }
}

/* Grid side of a finished recording: the track starts looping now, at
 * play_position; the take may still be completing the delayed loop body. */
static void complete_track(uint8_t track, uint32_t length, uint32_t play_position)
{
    synth_looper_track_t *slot = &s_tracks[track];
    synth_looper_track_finish_recording(slot, length);
    slot->play_position = play_position % length;
    synth_metronome_set_enabled(&s_record_metronome, false, s_current_bpm, s_global_frame);
    if (s_master_length_frames == 0) {
        s_master_length_frames = length;
        s_master_anchor_frame = s_take_anchor_frame;
    }
    set_message(SYNTH_LOOPER_MESSAGE_PLAYING, 0, 0);
    s_current_track = next_empty_track(track);
}

/* The grid reached the target, or an open take filled the buffer. */
static void close_recording_at_grid(uint8_t track)
{
    synth_looper_track_t *slot = &s_tracks[track];
    uint32_t elapsed = slot->record_position;
    uint32_t length = slot->target_record_frames;
    if (length == 0) {
        length = synth_looper_take_snap_length(elapsed, length_unit_frames(), s_capacity_frames);
    }
    set_take_length(track, length);
    complete_track(track, length, elapsed - length);
}

/* Stop click. Layers always span exactly one loop, so they end at the loop
 * point. The first loop snaps to the nearest whole bar or beat: rounding up
 * keeps recording to that point, rounding down starts the loop at once. */
static void stop_recording_by_click(uint8_t track)
{
    synth_looper_track_t *slot = &s_tracks[track];
    if (s_master_length_frames > 0) {
        set_message(SYNTH_LOOPER_MESSAGE_ENDS_AT_LOOP, 0, 0);
        return;
    }
    uint32_t elapsed = slot->record_position;
    if (elapsed < ms_to_frames(MIN_RECORD_MS)) {
        clear_track(track);
        set_message(SYNTH_LOOPER_MESSAGE_TOO_SHORT, 0, 0);
        return;
    }
    uint32_t length = synth_looper_take_snap_length(elapsed, length_unit_frames(), s_capacity_frames);
    set_take_length(track, length);
    if (length > elapsed) {
        slot->target_record_frames = length;
        return;
    }
    complete_track(track, length, elapsed - length);
}

static void arm_waiting_track(uint8_t track, uint16_t bpm)
{
    synth_looper_track_t *slot = &s_tracks[track];
    uint32_t target_frames = 0;

    if (s_master_length_frames > 0 && track != 0) {
        slot->record_armed = true;
        slot->sync_to_master = true;
        slot->record_start_frame = 0;
        slot->target_record_frames = s_master_length_frames;
        slot->count_in_beat_frames = 0;
        set_message(SYNTH_LOOPER_MESSAGE_SYNC_WAIT, 0, 0);
        return;
    }

    if (s_bar_count > 0) {
        target_frames = bars_to_frames(s_bar_count, bpm);
        if (target_frames > s_capacity_frames) {
            target_frames = s_capacity_frames;
        }
        slot->record_armed = true;
        slot->sync_to_master = false;
        uint32_t beat_count = synth_metronome_beat_frames(s_sample_rate_hz, bpm);
        uint64_t count_in_length = (uint64_t)beat_count * 4ULL;
        slot->count_in_beat_frames = beat_count;
        slot->record_start_frame = s_global_frame + count_in_length;
        if (s_beat_grid_valid) {
            /* A full count-in, then the beat's next bar line. */
            slot->record_start_frame = next_beat_grid_line(slot->record_start_frame, 16U);
        }
        slot->target_record_frames = target_frames;
        if (s_record_metronome_enabled) {
            synth_metronome_set_enabled(&s_record_metronome,
                                       true,
                                       bpm,
                                       slot->record_start_frame - count_in_length);
        }
        set_message(SYNTH_LOOPER_MESSAGE_COUNT_IN, 0, 0);
        return;
    }

    if (s_beat_grid_valid) {
        /* Free length over a running beat starts on its next beat. */
        slot->record_armed = true;
        slot->sync_to_master = false;
        slot->count_in_beat_frames = 0;
        slot->target_record_frames = 0;
        slot->record_start_frame = next_beat_grid_line(s_global_frame, 4U);
        set_message(SYNTH_LOOPER_MESSAGE_SYNC_WAIT, 0, 0);
        return;
    }

    start_recording(track, 0);
}

static bool process_track_should_start(const synth_looper_track_t *track,
                                       uint64_t global_frame,
                                       bool master_cycle_start)
{
    if (track->state != SYNTH_LOOPER_TRACK_WAITING || !track->record_armed) {
        return false;
    }
    if (track->sync_to_master) {
        return master_cycle_start;
    }
    return global_frame >= track->record_start_frame;
}

static void start_process_recording(uint8_t track_index,
                                    synth_metronome_t *record_metronome,
                                    bool record_metronome_enabled,
                                    uint16_t bpm,
                                    uint64_t global_frame)
{
    synth_looper_track_t *track = &s_tracks[track_index];
    uint32_t target_frames = track->target_record_frames;
    synth_looper_track_start_recording(track, target_frames);
    begin_take(track_index, target_frames, global_frame);
    if (record_metronome_enabled && !synth_metronome_is_enabled(record_metronome)) {
        synth_metronome_set_enabled(record_metronome, true, bpm, global_frame);
    }
}

static bool process_record_metronome_should_run(const synth_looper_track_t *tracks)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        if (record_metronome_should_run_for_track(tracks[track].state,
                                                  tracks[track].record_armed,
                                                  tracks[track].sync_to_master,
                                                  tracks[track].count_in_beat_frames)) {
            return true;
        }
    }
    return false;
}

/* Takes record the synth bus in mono (no drums, no loop playback). */
static int16_t live_mono_sample(const int16_t *block, uint32_t frame, uint8_t channels)
{
    uint32_t sample_index = frame * channels;
    int32_t mono = block[sample_index];
    if (channels > 1) {
        mono = (mono + block[sample_index + 1]) / 2;
    }
    return synth_looper_dsp_encode_sample(mono);
}

static void update_atomic_max(_Atomic uint32_t *maximum, uint32_t value)
{
    uint32_t current = atomic_load_explicit(maximum, memory_order_relaxed);
    while (current < value &&
           !atomic_compare_exchange_weak_explicit(maximum,
                                                  &current,
                                                  value,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static void build_runtime_view(synth_looper_runtime_view_t *view)
{
    *view = (synth_looper_runtime_view_t){
        .global_frame = s_global_frame,
        .master_length_frames = s_master_length_frames,
        .master_anchor_frame = s_master_anchor_frame,
        .audio_block = atomic_load_explicit(&s_audio_block, memory_order_relaxed),
        .current_track = s_current_track,
        .bar_count = s_bar_count,
        .message_code = s_message_code,
        .message_arg0 = s_message_arg0,
        .message_arg1 = s_message_arg1,
        .paused = s_paused,
        .metronome_enabled = synth_metronome_is_enabled(&s_global_metronome),
        .record_metronome_enabled = s_record_metronome_enabled,
        .record_metronome_active = synth_metronome_is_enabled(&s_record_metronome),
    };
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    view->meter = s_meter;
#endif

    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        view->track_state[track] = s_tracks[track].state;
        view->track_length_frames[track] = s_tracks[track].length_frames;
        view->track_position_frames[track] =
            s_tracks[track].state == SYNTH_LOOPER_TRACK_RECORDING ?
                s_tracks[track].record_position :
                s_tracks[track].play_position;
        view->track_volume_percent[track] = s_tracks[track].volume_percent;
    }
    view->mute_mask = s_mute_mask;
    view->solo_mask = s_solo_mask;

    const synth_looper_track_t *current = &s_tracks[s_current_track];
    view->target_record_frames = current->target_record_frames;
    view->record_start_frame = current->record_start_frame;
    view->count_in_beat_frames = current->count_in_beat_frames;
    view->current_track_armed = current->record_armed;
    view->waiting_for_sync = current->state == SYNTH_LOOPER_TRACK_WAITING &&
                             current->record_armed &&
                             current->sync_to_master;
}

static void publish_runtime_view(void)
{
    synth_looper_runtime_view_t view;
    build_runtime_view(&view);
    synth_looper_snapshot_publish(&s_snapshot_store, &view);
}

esp_err_t synth_looper_init(uint32_t sample_rate_hz, uint8_t channel_count)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (sample_rate_hz == 0 || channel_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_sample_rate_hz = sample_rate_hz;
    s_channel_count = channel_count;
    s_capacity_frames = sample_rate_hz * SYNTH_LOOPER_MAX_SECONDS;
    synth_looper_take_init(&s_take, ms_to_frames(RECORD_LATENCY_MS), ms_to_frames(SEAM_MS));
    s_take_track = UINT8_MAX;
    synth_metronome_init(&s_global_metronome, sample_rate_hz);
    synth_metronome_init(&s_record_metronome, sample_rate_hz);
    synth_looper_command_queue_init(&s_command_queue);
    atomic_init(&s_audio_block, 0);
    atomic_init(&s_max_command_latency_blocks, 0);
    atomic_init(&s_max_process_us, 0);
    atomic_init(&s_process_over_budget, 0);
    clear_all();
    esp_err_t allocation_err = allocate_all_track_buffers();
    if (allocation_err != ESP_OK) {
        free_track_buffers();
        ESP_LOGE(TAG, "looper preallocation failed: %s", esp_err_to_name(allocation_err));
        return allocation_err;
    }
    s_initialized = true;
    set_message(SYNTH_LOOPER_MESSAGE_EMPTY, 0, 0);
    synth_looper_runtime_view_t initial_view;
    build_runtime_view(&initial_view);
    synth_looper_snapshot_store_init(&s_snapshot_store, &initial_view);

    ESP_LOGI(TAG,
             "looper ready tracks=%u max_seconds=%u sample_rate=%lu channels=%u storage=mono-int16 bytes_per_track=%lu preallocated=%lu",
             (unsigned)SYNTH_LOOPER_TRACK_COUNT,
             (unsigned)SYNTH_LOOPER_MAX_SECONDS,
             (unsigned long)s_sample_rate_hz,
             (unsigned)s_channel_count,
             (unsigned long)track_buffer_bytes(),
             (unsigned long)(track_buffer_bytes() * SYNTH_LOOPER_TRACK_COUNT));
    return ESP_OK;
}

static esp_err_t enqueue_command(synth_looper_command_t command)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    command.submitted_block = atomic_load_explicit(&s_audio_block, memory_order_acquire);
    return synth_looper_command_queue_push(&s_command_queue, &command);
}

static void apply_click_command(uint16_t bpm)
{
    set_current_bpm(bpm);
    synth_looper_track_t *slot = &s_tracks[s_current_track];
    switch (slot->state) {
    case SYNTH_LOOPER_TRACK_EMPTY:
        slot->state = SYNTH_LOOPER_TRACK_WAITING;
        slot->record_armed = false;
        slot->sync_to_master = false;
        slot->record_position = 0;
        slot->play_position = 0;
        slot->length_frames = 0;
        slot->record_start_frame = 0;
        slot->target_record_frames = 0;
        slot->count_in_beat_frames = 0;
        set_message(SYNTH_LOOPER_MESSAGE_WAITING, 0, 0);
        break;
    case SYNTH_LOOPER_TRACK_WAITING:
        arm_waiting_track(s_current_track, bpm);
        break;
    case SYNTH_LOOPER_TRACK_RECORDING:
        stop_recording_by_click(s_current_track);
        break;
    case SYNTH_LOOPER_TRACK_LOOPING:
        if (s_current_track == 0) {
            clear_all();
        } else {
            slot->state = SYNTH_LOOPER_TRACK_OFF;
            slot->play_position = 0;
            set_message(SYNTH_LOOPER_MESSAGE_OFF, 0, 0);
        }
        break;
    case SYNTH_LOOPER_TRACK_OFF:
        if (s_current_track == 0) {
            clear_all();
        } else {
            clear_track(s_current_track);
            set_message(SYNTH_LOOPER_MESSAGE_CLEARED, 0, 0);
        }
        break;
    default:
        break;
    }
    if (!has_any_loop()) {
        s_paused = false;
    }
}

static void apply_command(const synth_looper_command_t *command)
{
    uint32_t audio_block = atomic_load_explicit(&s_audio_block, memory_order_relaxed);
    update_atomic_max(&s_max_command_latency_blocks, audio_block - command->submitted_block);

    switch (command->type) {
    case SYNTH_LOOPER_COMMAND_CLICK:
        apply_click_command(command->bpm);
        break;
    case SYNTH_LOOPER_COMMAND_SELECT_TRACK: {
        int value = (int)s_current_track + command->value;
        while (value < 0) {
            value += SYNTH_LOOPER_TRACK_COUNT;
        }
        s_current_track = (uint8_t)(value % SYNTH_LOOPER_TRACK_COUNT);
        set_message(SYNTH_LOOPER_MESSAGE_TRACK, (uint8_t)(s_current_track + 1U), 0);
        break;
    }
    case SYNTH_LOOPER_COMMAND_SET_TRACK_VOLUME:
    case SYNTH_LOOPER_COMMAND_SET_CURRENT_TRACK_VOLUME: {
        uint8_t track = command->type == SYNTH_LOOPER_COMMAND_SET_CURRENT_TRACK_VOLUME ?
            s_current_track : command->track;
        uint8_t percent = command->value > 100 ? 100 : (uint8_t)command->value;
        s_current_track = track;
        if (s_tracks[track].volume_percent != percent) {
            s_tracks[track].volume_percent = percent;
            set_message(SYNTH_LOOPER_MESSAGE_TRACK_VOLUME, (uint8_t)(track + 1U), percent);
        }
        break;
    }
    case SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_MUTE: {
        uint8_t bit = track_bit(command->track);
        s_current_track = command->track;
        s_mute_mask ^= bit;
        set_message((s_mute_mask & bit) != 0 ?
                        SYNTH_LOOPER_MESSAGE_TRACK_MUTE :
                        SYNTH_LOOPER_MESSAGE_TRACK_ON,
                    (uint8_t)(command->track + 1U),
                    0);
        break;
    }
    case SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_SOLO: {
        uint8_t bit = track_bit(command->track);
        s_current_track = command->track;
        s_solo_mask ^= bit;
        set_message((s_solo_mask & bit) != 0 ?
                        SYNTH_LOOPER_MESSAGE_TRACK_SOLO_ON :
                        SYNTH_LOOPER_MESSAGE_TRACK_SOLO_OFF,
                    (uint8_t)(command->track + 1U),
                    0);
        break;
    }
    case SYNTH_LOOPER_COMMAND_TOGGLE_METRONOME: {
        set_current_bpm(command->bpm);
        bool enabled = synth_metronome_toggle(&s_global_metronome,
                                              command->bpm,
                                              s_global_frame);
        set_message(enabled ? SYNTH_LOOPER_MESSAGE_METRONOME_ON :
                              SYNTH_LOOPER_MESSAGE_METRONOME_OFF,
                    0,
                    0);
        break;
    }
    case SYNTH_LOOPER_COMMAND_TOGGLE_RECORD_METRONOME:
        set_current_bpm(command->bpm);
        s_record_metronome_enabled = !s_record_metronome_enabled;
        if (!s_record_metronome_enabled) {
            synth_metronome_set_enabled(&s_record_metronome, false, s_current_bpm, s_global_frame);
        } else if (record_metronome_should_run()) {
            synth_metronome_set_enabled(&s_record_metronome, true, s_current_bpm, s_global_frame);
        } else {
            synth_metronome_set_timing(&s_record_metronome, s_current_bpm, s_global_frame);
        }
        set_message(s_record_metronome_enabled ?
                        SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_ON :
                        SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_OFF,
                    0,
                    0);
        break;
    case SYNTH_LOOPER_COMMAND_SET_METRONOME_BPM:
        set_current_bpm(command->bpm);
        if (synth_metronome_is_enabled(&s_global_metronome)) {
            synth_metronome_set_bpm_preserve_phase(&s_global_metronome,
                                                   s_current_bpm,
                                                   s_global_frame);
        }
        if (synth_metronome_is_enabled(&s_record_metronome)) {
            synth_metronome_set_bpm_preserve_phase(&s_record_metronome,
                                                   s_current_bpm,
                                                   s_global_frame);
        } else if (s_record_metronome_enabled) {
            synth_metronome_set_timing(&s_record_metronome, s_current_bpm, s_global_frame);
        }
        break;
    case SYNTH_LOOPER_COMMAND_ADJUST_BARS: {
        int value = (int)s_bar_count + command->value;
        if (value < 0) {
            value = 0;
        } else if (value > SYNTH_LOOPER_MAX_BARS) {
            value = SYNTH_LOOPER_MAX_BARS;
        }
        s_bar_count = (uint8_t)value;
        set_message(s_bar_count == 0 ? SYNTH_LOOPER_MESSAGE_FREE_LENGTH : SYNTH_LOOPER_MESSAGE_BARS,
                    s_bar_count,
                    0);
        break;
    }
    case SYNTH_LOOPER_COMMAND_TOGGLE_PAUSE:
        if (synth_looper_take_active(&s_take)) {
            /* A take finishes its delayed body in real time; pausing would
             * splice later audio into the loop end. */
            set_message(SYNTH_LOOPER_MESSAGE_RECORDING, 0, 0);
        } else if (has_any_loop()) {
            s_paused = !s_paused;
            if (!s_paused) {
                /* Rejoin the shared grid (the live beat keeps running while
                 * loops are paused): every loop resumes where it would be. */
                for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
                    synth_looper_track_t *slot = &s_tracks[i];
                    if (slot->state == SYNTH_LOOPER_TRACK_LOOPING &&
                        slot->length_frames == s_master_length_frames && s_master_length_frames > 0 &&
                        s_global_frame >= s_master_anchor_frame) {
                        slot->play_position =
                            (uint32_t)((s_global_frame - s_master_anchor_frame) % s_master_length_frames);
                    }
                }
            }
            set_message(s_paused ? SYNTH_LOOPER_MESSAGE_PAUSED : SYNTH_LOOPER_MESSAGE_RESUMED, 0, 0);
        } else {
            s_paused = false;
            set_message(SYNTH_LOOPER_MESSAGE_NO_LOOP, 0, 0);
        }
        break;
    case SYNTH_LOOPER_COMMAND_CLEAR_ALL:
        clear_all();
        break;
    case SYNTH_LOOPER_COMMAND_SET_BEAT_GRID:
        set_current_bpm(command->bpm);
        s_beat_grid_valid = command->value != 0;
        s_beat_anchor_frame = command->frame;
        break;
    case SYNTH_LOOPER_COMMAND_CLEAR_TRACK:
        if (command->track == 0) {
            clear_all();
        } else {
            clear_track(command->track);
            s_current_track = command->track;
            if (!has_any_loop()) {
                s_paused = false;
            }
            set_message(SYNTH_LOOPER_MESSAGE_CLEARED, 0, 0);
        }
        break;
    default:
        break;
    }
}

static void drain_commands(void)
{
    synth_looper_command_t command;
    while (synth_looper_command_queue_pop(&s_command_queue, &command)) {
        apply_command(&command);
    }
}

void synth_looper_process_audio(int16_t *block, const int16_t *record_block, uint32_t frames, uint8_t channels)
{
    if (!s_initialized || block == NULL || frames == 0 || channels == 0) {
        return;
    }
    if (record_block == NULL) {
        record_block = block;
    }

    drain_commands();

    bool has_active_track = false;
    bool paused = false;
    bool record_metronome_enabled = false;
    uint16_t current_bpm = 120;
    synth_metronome_t global_metronome = {0};
    synth_metronome_t record_metronome = {0};
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    uint32_t live_peak = 0;
    uint32_t loop_peak = 0;
    uint32_t output_peak = 0;
    uint8_t max_audible_tracks = 0;
    synth_looper_meter_t block_meter = {0};
#endif

    uint64_t block_start = s_global_frame;
    s_global_frame += frames;
    paused = s_paused;
    current_bpm = s_current_bpm;
    record_metronome_enabled = s_record_metronome_enabled;
    global_metronome = s_global_metronome;
    record_metronome = s_record_metronome;
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        if (s_tracks[i].state == SYNTH_LOOPER_TRACK_RECORDING ||
            s_tracks[i].state == SYNTH_LOOPER_TRACK_LOOPING ||
            (s_tracks[i].state == SYNTH_LOOPER_TRACK_WAITING &&
             s_tracks[i].record_armed)) {
            has_active_track = true;
        }
    }
    if (paused ||
        (!has_active_track &&
         !synth_metronome_is_enabled(&global_metronome) &&
         !synth_metronome_is_enabled(&record_metronome))) {
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        memset(&s_meter, 0, sizeof(s_meter));
#endif
        atomic_fetch_add_explicit(&s_audio_block, 1, memory_order_release);
        publish_runtime_view();
        return;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        uint64_t global_frame = block_start + frame;
        bool just_completed[SYNTH_LOOPER_TRACK_COUNT] = {0};
        bool master_cycle_start = s_master_length_frames > 0 &&
                                  s_tracks[0].state == SYNTH_LOOPER_TRACK_LOOPING &&
                                  s_tracks[0].play_position == 0;
        int32_t click_mix = 0;
        int32_t loop_mix = 0;
        uint8_t active_playback_tracks = 0;

        click_mix = synth_metronome_sample(&global_metronome, global_frame);
        if (!synth_metronome_is_enabled(&global_metronome)) {
            click_mix += synth_metronome_sample(&record_metronome, global_frame);
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (process_track_should_start(&s_tracks[track], global_frame, master_cycle_start)) {
                start_process_recording(track,
                                        &record_metronome,
                                        record_metronome_enabled,
                                        current_bpm,
                                        global_frame);
                set_message(SYNTH_LOOPER_MESSAGE_RECORDING, 0, 0);
            }
        }

        /* Audio goes through the take (latency-shifted, seam-folded); the
         * track's RECORDING state only advances the grid. */
        synth_looper_take_status_t take_status =
            synth_looper_take_capture(&s_take, live_mono_sample(record_block, frame, channels));
        if (take_status == SYNTH_LOOPER_TAKE_DONE) {
            s_take_track = UINT8_MAX;
        } else if (take_status == SYNTH_LOOPER_TAKE_FULL && s_take_track < SYNTH_LOOPER_TRACK_COUNT) {
            set_take_length(s_take_track,
                            synth_looper_take_snap_length(s_capacity_frames,
                                                          length_unit_frames(),
                                                          s_capacity_frames));
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (synth_looper_track_advance_recording(&s_tracks[track], s_capacity_frames)) {
                close_recording_at_grid(track);
                just_completed[track] = true;
            }
        }

        if (!process_record_metronome_should_run(s_tracks)) {
            synth_metronome_set_enabled(&record_metronome, false, current_bpm, global_frame);
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (just_completed[track]) {
                continue;
            }
            if (synth_looper_track_mix_playback(&s_tracks[track],
                                                track_is_audible(track),
                                                &loop_mix)) {
                ++active_playback_tracks;
            }
        }
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        if (active_playback_tracks > max_audible_tracks) {
            max_audible_tracks = active_playback_tracks;
        }
#endif

        if (active_playback_tracks > 0) {
            loop_mix = synth_looper_dsp_apply_gain_q8(
                loop_mix,
                synth_looper_dsp_bus_gain_q8(active_playback_tracks));
        }
        loop_mix += click_mix;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        synth_looper_dsp_meter_peak(&loop_peak, loop_mix);
#endif

        uint32_t sample_index = frame * channels;
        bool mix_path_active = active_playback_tracks > 0 || click_mix != 0;
        if (!mix_path_active) {
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            for (uint8_t channel = 0; channel < channels; ++channel) {
                int32_t live = block[sample_index + channel];
                synth_looper_dsp_meter_peak(&live_peak, live);
                synth_looper_dsp_meter_peak(&output_peak, live);
            }
#endif
            continue;
        }

        for (uint8_t channel = 0; channel < channels; ++channel) {
            int32_t live_input = block[sample_index + channel];
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            synth_looper_dsp_meter_peak(&live_peak, live_input);
#endif
            int32_t mixed = live_input + loop_mix;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            block[sample_index + channel] = synth_looper_dsp_saturate(mixed, &block_meter);
            synth_looper_dsp_meter_peak(&output_peak, block[sample_index + channel]);
#else
            block[sample_index + channel] = synth_looper_dsp_saturate(mixed, NULL);
#endif
        }
    }

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    block_meter.live_peak_percent = synth_looper_dsp_peak_percent(live_peak);
    block_meter.loop_peak_percent = synth_looper_dsp_peak_percent(loop_peak);
    block_meter.output_peak_percent = synth_looper_dsp_peak_percent(output_peak);
    log_meter_if_due(&block_meter, block_start, max_audible_tracks);
#endif

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    s_meter = block_meter;
#endif
    s_record_metronome = record_metronome;
    atomic_fetch_add_explicit(&s_audio_block, 1, memory_order_release);
    publish_runtime_view();
}

esp_err_t synth_looper_set_beat_grid(bool running, uint64_t anchor_frame, uint16_t bpm)
{
    if (bpm == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_SET_BEAT_GRID,
        .value = running ? 1 : 0,
        .bpm = bpm,
        .frame = anchor_frame,
    });
}

esp_err_t synth_looper_click(uint16_t bpm)
{
    if (bpm == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_CLICK,
        .bpm = bpm,
    });
}

esp_err_t synth_looper_select_track(int8_t steps)
{
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_SELECT_TRACK,
        .value = steps,
    });
}

esp_err_t synth_looper_set_track_volume(uint8_t track, uint8_t percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        percent = 100;
    }

    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_SET_TRACK_VOLUME,
        .track = track,
        .value = percent,
    });
}

esp_err_t synth_looper_set_current_track_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_SET_CURRENT_TRACK_VOLUME,
        .value = percent,
    });
}

esp_err_t synth_looper_toggle_track_mute(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_MUTE,
        .track = track,
    });
}

esp_err_t synth_looper_toggle_track_solo(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_SOLO,
        .track = track,
    });
}

esp_err_t synth_looper_toggle_metronome(uint16_t bpm)
{
    if (bpm == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_TOGGLE_METRONOME,
        .bpm = bpm,
    });
}

esp_err_t synth_looper_toggle_record_metronome(uint16_t bpm)
{
    if (bpm == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_TOGGLE_RECORD_METRONOME,
        .bpm = bpm,
    });
}

esp_err_t synth_looper_set_metronome_bpm(uint16_t bpm)
{
    if (bpm == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_SET_METRONOME_BPM,
        .bpm = bpm,
    });
}

esp_err_t synth_looper_adjust_bars(int8_t steps)
{
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_ADJUST_BARS,
        .value = steps,
    });
}

esp_err_t synth_looper_toggle_pause(void)
{
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_TOGGLE_PAUSE,
    });
}

esp_err_t synth_looper_clear_all(void)
{
    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_CLEAR_ALL,
    });
}

esp_err_t synth_looper_clear_track(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    return enqueue_command((synth_looper_command_t){
        .type = SYNTH_LOOPER_COMMAND_CLEAR_TRACK,
        .track = track,
    });
}

bool synth_looper_current_track_is_waiting(void)
{
    if (!s_initialized) {
        return false;
    }

    synth_looper_runtime_view_t view;
    return synth_looper_snapshot_read(&s_snapshot_store, &view) &&
           view.track_state[view.current_track] == SYNTH_LOOPER_TRACK_WAITING;
}

bool synth_looper_take_in_progress(void)
{
    if (!s_initialized) {
        return false;
    }

    synth_looper_runtime_view_t view;
    if (!synth_looper_snapshot_read(&s_snapshot_store, &view)) {
        return false;
    }
    if (view.current_track_armed) {
        return true;
    }
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        if (view.track_state[track] == SYNTH_LOOPER_TRACK_RECORDING) {
            return true;
        }
    }
    return false;
}

void synth_looper_get_snapshot(synth_looper_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_initialized) {
        strlcpy(snapshot->message, "LOOPER OFF", sizeof(snapshot->message));
        return;
    }

    synth_looper_runtime_view_t view;
    if (synth_looper_snapshot_read(&s_snapshot_store, &view)) {
        synth_looper_view_to_snapshot(s_sample_rate_hz, &view, snapshot);
    }
}

uint64_t synth_looper_next_block_frame(void)
{
    return s_global_frame;
}

bool synth_looper_get_clock(synth_looper_clock_t *clock)
{
    if (clock == NULL || !s_initialized) {
        return false;
    }
    synth_looper_runtime_view_t view;
    if (!synth_looper_snapshot_read(&s_snapshot_store, &view)) {
        return false;
    }
    *clock = (synth_looper_clock_t){
        .sample_rate_hz = s_sample_rate_hz,
        .frame = view.global_frame,
        .has_loop = view.master_length_frames > 0,
        .loop_anchor_frame = view.master_anchor_frame,
        .loop_length_frames = view.master_length_frames,
    };
    return true;
}

void synth_looper_get_diagnostics(synth_looper_diagnostics_t *diagnostics)
{
    if (diagnostics == NULL) {
        return;
    }

    *diagnostics = (synth_looper_diagnostics_t){
        .command_queue_high_water = synth_looper_command_queue_high_water(&s_command_queue),
        .command_rejected = synth_looper_command_queue_rejected(&s_command_queue),
        .command_depth = synth_looper_command_queue_depth(&s_command_queue),
        .max_command_latency_blocks =
            atomic_load_explicit(&s_max_command_latency_blocks, memory_order_relaxed),
        .snapshot_reader_retries = synth_looper_snapshot_reader_retries(&s_snapshot_store),
        .snapshot_publish_skipped = synth_looper_snapshot_publish_skipped(&s_snapshot_store),
        .max_process_us = atomic_load_explicit(&s_max_process_us, memory_order_relaxed),
        .process_over_budget =
            atomic_load_explicit(&s_process_over_budget, memory_order_relaxed),
    };
}

void synth_looper_record_process_time(uint32_t process_us, uint32_t audio_block_budget_us)
{
    update_atomic_max(&s_max_process_us, process_us);
    if (process_us > audio_block_budget_us) {
        atomic_fetch_add_explicit(&s_process_over_budget, 1, memory_order_relaxed);
    }
}

const char *synth_looper_track_state_name(synth_looper_track_state_t state)
{
    switch (state) {
    case SYNTH_LOOPER_TRACK_EMPTY:
        return "EMPTY";
    case SYNTH_LOOPER_TRACK_WAITING:
        return "WAITING";
    case SYNTH_LOOPER_TRACK_RECORDING:
        return "RECORDING";
    case SYNTH_LOOPER_TRACK_LOOPING:
        return "LOOPING";
    case SYNTH_LOOPER_TRACK_OFF:
        return "OFF";
    default:
        return "?";
    }
}

const char *synth_looper_track_state_short_name(synth_looper_track_state_t state)
{
    switch (state) {
    case SYNTH_LOOPER_TRACK_EMPTY:
        return "-";
    case SYNTH_LOOPER_TRACK_WAITING:
        return "WAIT";
    case SYNTH_LOOPER_TRACK_RECORDING:
        return "REC";
    case SYNTH_LOOPER_TRACK_LOOPING:
        return "PLAY";
    case SYNTH_LOOPER_TRACK_OFF:
        return "OFF";
    default:
        return "?";
    }
}
