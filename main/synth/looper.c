#include "synth/looper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "synth/crossfade.h"
#include "synth/metronome.h"

typedef struct {
    int16_t *samples;
    synth_looper_track_state_t state;
    bool record_armed;
    bool sync_to_master;
    uint32_t length_frames;
    uint32_t play_position;
    uint32_t record_position;
    uint64_t record_start_frame;
    uint32_t target_record_frames;
    uint32_t count_in_beat_frames;
    uint8_t volume_percent;
} synth_looper_track_t;

typedef struct {
    int16_t *samples;
    synth_looper_track_state_t state;
    uint32_t length_frames;
    uint32_t play_position;
    uint32_t record_position;
    uint32_t target_record_frames;
    bool record_armed;
    bool sync_to_master;
    uint64_t record_start_frame;
    uint32_t count_in_beat_frames;
    uint8_t volume_percent;
    bool audible;
} synth_looper_process_track_t;

static const char *TAG = "SYNTH_LOOPER";

static const uint16_t GAIN_Q8_UNITY = 256;
static const uint16_t LIVE_MIX_GAIN_Q8 = 192;
static const uint8_t LOOP_TRACK_DEFAULT_VOLUME_PERCENT = 85;
static const uint32_t MIN_RECORD_MS = 120;
static const uint32_t LOOP_SEAM_CROSSFADE_MS = 50;
static const int32_t SOFT_LIMIT_THRESHOLD = 24576;
static const uint32_t SOFT_LIMIT_KNEE = 8191;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static const uint32_t METER_LOG_INTERVAL_MS = 1000;
static const uint32_t CLIP_LOG_INTERVAL_MS = 500;
#endif
static const uint16_t LOOP_BUS_GAIN_BY_TRACKS_Q8[SYNTH_LOOPER_TRACK_COUNT + 1] = {
    0,
    192,
    160,
    142,
    128,
    118,
    110,
};

static synth_looper_track_t s_tracks[SYNTH_LOOPER_TRACK_COUNT];
static uint32_t s_sample_rate_hz;
static uint8_t s_channel_count;
static uint32_t s_capacity_frames;
static uint32_t s_master_length_frames;
static uint64_t s_global_frame;
static uint8_t s_current_track;
static uint8_t s_bar_count;
static bool s_paused;
static bool s_initialized;
static uint16_t s_current_bpm = 120;
static synth_metronome_t s_global_metronome;
static synth_metronome_t s_record_metronome;
static bool s_record_metronome_enabled;
static uint8_t s_mute_mask;
static uint8_t s_solo_mask;
static char s_message[22] = "EMPTY";
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static synth_looper_meter_t s_meter;
static uint64_t s_next_meter_log_frame;
static uint64_t s_next_clip_log_frame;
#endif
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t track_bit(uint8_t track);

static uint32_t ms_to_frames(uint32_t ms)
{
    return (uint32_t)(((uint64_t)ms * s_sample_rate_hz) / 1000ULL);
}

static uint32_t frames_to_ms(uint32_t frames)
{
    if (s_sample_rate_hz == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)frames * 1000ULL) / s_sample_rate_hz);
}

static uint32_t frames64_to_ms(uint64_t frames)
{
    if (s_sample_rate_hz == 0) {
        return 0;
    }
    uint64_t ms = (frames * 1000ULL) / s_sample_rate_hz;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static uint32_t bars_to_frames(uint8_t bars, uint16_t bpm)
{
    if (bars == 0 || bpm == 0) {
        return 0;
    }
    uint64_t beats = (uint64_t)bars * 4ULL;
    return (uint32_t)((beats * 60ULL * (uint64_t)s_sample_rate_hz) / (uint64_t)bpm);
}

static void set_current_bpm_locked(uint16_t bpm)
{
    s_current_bpm = bpm == 0 ? 120 : bpm;
}

static size_t track_buffer_bytes(void)
{
    return (size_t)s_capacity_frames * sizeof(int16_t);
}

static void set_message_locked(const char *message)
{
    if (message == NULL) {
        return;
    }
    strlcpy(s_message, message, sizeof(s_message));
}

static bool has_any_loop_locked(void)
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

static bool record_metronome_should_run_locked(void)
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

static void clear_track_locked(uint8_t track)
{
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return;
    }

    synth_looper_track_t *slot = &s_tracks[track];
    slot->state = SYNTH_LOOPER_TRACK_EMPTY;
    slot->record_armed = false;
    slot->sync_to_master = false;
    slot->length_frames = 0;
    slot->play_position = 0;
    slot->record_position = 0;
    slot->record_start_frame = 0;
    slot->target_record_frames = 0;
    slot->count_in_beat_frames = 0;
    slot->volume_percent = LOOP_TRACK_DEFAULT_VOLUME_PERCENT;
    s_mute_mask &= (uint8_t)~track_bit(track);
    s_solo_mask &= (uint8_t)~track_bit(track);
}

static void clear_all_locked(void)
{
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        clear_track_locked(i);
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
    set_message_locked("CLEARED");
}

static uint8_t next_empty_track_locked(uint8_t from_track)
{
    for (uint8_t offset = 1; offset <= SYNTH_LOOPER_TRACK_COUNT; ++offset) {
        uint8_t track = (uint8_t)((from_track + offset) % SYNTH_LOOPER_TRACK_COUNT);
        if (s_tracks[track].state == SYNTH_LOOPER_TRACK_EMPTY) {
            return track;
        }
    }
    return from_track;
}

static uint8_t first_empty_track_locked(void)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        if (s_tracks[track].state == SYNTH_LOOPER_TRACK_EMPTY) {
            return track;
        }
    }
    return SYNTH_LOOPER_TRACK_COUNT;
}

static int16_t encode_sample(int32_t sample)
{
    if (sample > 32767) {
        sample = 32767;
    } else if (sample < -32768) {
        sample = -32768;
    }
    return (int16_t)sample;
}

static int32_t decode_sample(int16_t sample)
{
    return (int32_t)sample;
}

static int32_t apply_gain_q8(int32_t sample, uint16_t gain_q8)
{
    if (gain_q8 == GAIN_Q8_UNITY) {
        return sample;
    }
    return (sample * (int32_t)gain_q8) / (int32_t)GAIN_Q8_UNITY;
}

static int32_t apply_percent_gain(int32_t sample, uint8_t percent)
{
    if (percent >= 100) {
        return sample;
    }
    return (sample * (int32_t)percent) / 100;
}

static uint16_t loop_bus_gain_q8(uint8_t active_tracks)
{
    if (active_tracks > SYNTH_LOOPER_TRACK_COUNT) {
        active_tracks = SYNTH_LOOPER_TRACK_COUNT;
    }
    return LOOP_BUS_GAIN_BY_TRACKS_Q8[active_tracks];
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

static uint32_t seam_crossfade_frames_for_length(uint32_t length_frames)
{
    uint32_t frames = ms_to_frames(LOOP_SEAM_CROSSFADE_MS);
    uint32_t max_frames = length_frames / 4U;
    if (frames > max_frames) {
        frames = max_frames;
    }
    return frames;
}

static uint32_t abs_sample_i32(int32_t sample)
{
    if (sample < 0) {
        return sample == INT32_MIN ? (uint32_t)INT32_MAX : (uint32_t)-sample;
    }
    return (uint32_t)sample;
}

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static void meter_peak(uint32_t *peak, int32_t sample)
{
    uint32_t magnitude = abs_sample_i32(sample);
    if (magnitude > *peak) {
        *peak = magnitude;
    }
}

static uint8_t peak_to_percent(uint32_t peak)
{
    if (peak >= 32767U) {
        return 100;
    }
    return (uint8_t)(((peak * 100U) + 16383U) / 32767U);
}

static void increment_meter_counter(uint16_t *counter)
{
    if (*counter < UINT16_MAX) {
        ++(*counter);
    }
}
#endif

static int16_t soft_limit_i16(int32_t sample, synth_looper_meter_t *meter)
{
    uint32_t magnitude = abs_sample_i32(sample);
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    if (magnitude > 32767U && meter != NULL) {
        increment_meter_counter(&meter->would_clip_samples);
    }
#endif
    if (magnitude <= (uint32_t)SOFT_LIMIT_THRESHOLD) {
        return (int16_t)sample;
    }

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    if (meter != NULL) {
        increment_meter_counter(&meter->limited_samples);
    }
#endif

    uint32_t over = magnitude - (uint32_t)SOFT_LIMIT_THRESHOLD;
    uint32_t shaped = (uint32_t)SOFT_LIMIT_THRESHOLD +
                      (uint32_t)(((uint64_t)over * SOFT_LIMIT_KNEE) /
                                 ((uint64_t)over + SOFT_LIMIT_KNEE));
    if (shaped > 32767U) {
        shaped = 32767U;
    }
    return sample < 0 ? (int16_t)-(int32_t)shaped : (int16_t)shaped;
}

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
static void log_meter_if_due(const synth_looper_meter_t *meter,
                             uint64_t block_start_frame,
                             uint8_t max_audible_tracks)
{
    if (meter == NULL || s_sample_rate_hz == 0) {
        return;
    }

    bool would_clip = meter->would_clip_samples > 0;
    uint64_t *next_log_frame = would_clip ? &s_next_clip_log_frame : &s_next_meter_log_frame;
    if (block_start_frame < *next_log_frame) {
        return;
    }

    uint32_t interval_frames = ms_to_frames(would_clip ? CLIP_LOG_INTERVAL_MS : METER_LOG_INTERVAL_MS);
    if (interval_frames == 0) {
        interval_frames = 1;
    }
    *next_log_frame = block_start_frame + interval_frames;

    if (would_clip) {
        ESP_LOGW(TAG,
                 "mix limiter live=%u%% loop=%u%% out=%u%% limited=%u would_clip=%u audible_tracks=%u",
                 (unsigned)meter->live_peak_percent,
                 (unsigned)meter->loop_peak_percent,
                 (unsigned)meter->output_peak_percent,
                 (unsigned)meter->limited_samples,
                 (unsigned)meter->would_clip_samples,
                 (unsigned)max_audible_tracks);
    } else {
        ESP_LOGD(TAG,
                 "mix meter live=%u%% loop=%u%% out=%u%% limited=%u audible_tracks=%u",
                 (unsigned)meter->live_peak_percent,
                 (unsigned)meter->loop_peak_percent,
                 (unsigned)meter->output_peak_percent,
                 (unsigned)meter->limited_samples,
                 (unsigned)max_audible_tracks);
    }
}
#endif

static esp_err_t ensure_track_buffer(uint8_t track)
{
    if (track >= SYNTH_LOOPER_TRACK_COUNT || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tracks[track].samples != NULL) {
        return ESP_OK;
    }

    size_t buffer_bytes = track_buffer_bytes();
    int16_t *buffer = heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(buffer_bytes, MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        portENTER_CRITICAL(&s_mux);
        set_message_locked("NO MEMORY");
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGW(TAG,
                 "failed to allocate %lu byte looper track buffer",
                 (unsigned long)buffer_bytes);
        return ESP_ERR_NO_MEM;
    }
    memset(buffer, 0, buffer_bytes);

    portENTER_CRITICAL(&s_mux);
    if (s_tracks[track].samples == NULL) {
        s_tracks[track].samples = buffer;
        buffer = NULL;
    }
    portEXIT_CRITICAL(&s_mux);

    if (buffer != NULL) {
        heap_caps_free(buffer);
    }
    return ESP_OK;
}

static esp_err_t allocate_all_track_buffers(void)
{
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        ESP_RETURN_ON_ERROR(ensure_track_buffer(track), TAG, "looper track buffer allocation failed");
    }
    return ESP_OK;
}

static void start_recording_locked(uint8_t track, uint32_t target_frames)
{
    synth_looper_track_t *slot = &s_tracks[track];
    slot->state = SYNTH_LOOPER_TRACK_RECORDING;
    slot->record_armed = false;
    slot->sync_to_master = false;
    slot->length_frames = 0;
    slot->play_position = 0;
    slot->record_position = 0;
    slot->record_start_frame = 0;
    slot->target_record_frames = target_frames;
    slot->count_in_beat_frames = 0;
    if (s_record_metronome_enabled) {
        synth_metronome_set_enabled(&s_record_metronome, true, s_current_bpm, s_global_frame);
    }

    set_message_locked("REC");
}

static void finalize_recording_locked(uint8_t track, bool auto_advance, bool completed_target)
{
    synth_looper_track_t *slot = &s_tracks[track];
    uint32_t min_frames = ms_to_frames(MIN_RECORD_MS);
    if (slot->record_position < min_frames) {
        clear_track_locked(track);
        set_message_locked("TOO SHORT");
        return;
    }

    uint32_t length = slot->record_position;
    if (completed_target && slot->target_record_frames > 0) {
        length = slot->target_record_frames;
    }
    if (completed_target && s_master_length_frames > 0 && track != 0) {
        length = s_master_length_frames;
    }
    if (length > s_capacity_frames) {
        length = s_capacity_frames;
    }

    slot->length_frames = length;
    slot->play_position = 0;
    slot->record_position = 0;
    slot->target_record_frames = 0;
    slot->record_start_frame = 0;
    slot->record_armed = false;
    slot->sync_to_master = false;
    slot->count_in_beat_frames = 0;
    slot->state = SYNTH_LOOPER_TRACK_LOOPING;
    synth_metronome_set_enabled(&s_record_metronome, false, s_current_bpm, s_global_frame);

    if (s_master_length_frames == 0 || track == 0) {
        s_master_length_frames = length;
    }

    set_message_locked("PLAY");

    if (auto_advance) {
        s_current_track = next_empty_track_locked(track);
    }
}

static void arm_waiting_track_locked(uint8_t track, uint16_t bpm)
{
    synth_looper_track_t *slot = &s_tracks[track];
    uint32_t target_frames = 0;

    if (s_master_length_frames > 0 && track != 0) {
        slot->record_armed = true;
        slot->sync_to_master = true;
        slot->record_start_frame = 0;
        slot->target_record_frames = s_master_length_frames;
        slot->count_in_beat_frames = 0;
        set_message_locked("SYNC WAIT");
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
        slot->target_record_frames = target_frames;
        if (s_record_metronome_enabled) {
            synth_metronome_set_enabled(&s_record_metronome,
                                       true,
                                       bpm,
                                       slot->record_start_frame - count_in_length);
        }
        set_message_locked("COUNT IN");
        return;
    }

    start_recording_locked(track, 0);
}

static bool process_track_should_start(const synth_looper_process_track_t *track,
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

static void start_process_recording(synth_looper_process_track_t *track,
                                    synth_metronome_t *record_metronome,
                                    bool record_metronome_enabled,
                                    uint16_t bpm,
                                    uint64_t global_frame)
{
    track->state = SYNTH_LOOPER_TRACK_RECORDING;
    track->record_armed = false;
    track->sync_to_master = false;
    track->length_frames = 0;
    track->play_position = 0;
    track->record_position = 0;
    track->record_start_frame = 0;
    track->count_in_beat_frames = 0;
    if (record_metronome_enabled && !synth_metronome_is_enabled(record_metronome)) {
        synth_metronome_set_enabled(record_metronome, true, bpm, global_frame);
    }
}

static void finish_process_recording(synth_looper_process_track_t *track,
                                     uint8_t track_index,
                                     bool completed_target,
                                     uint32_t master_length_frames)
{
    uint32_t length = track->record_position;
    if (completed_target && track->target_record_frames > 0) {
        length = track->target_record_frames;
    }
    if (completed_target && master_length_frames > 0 && track_index != 0) {
        length = master_length_frames;
    }
    if (length > s_capacity_frames) {
        length = s_capacity_frames;
    }

    track->state = SYNTH_LOOPER_TRACK_LOOPING;
    track->record_armed = false;
    track->sync_to_master = false;
    track->length_frames = length;
    track->play_position = 0;
    track->record_position = 0;
    track->record_start_frame = 0;
    track->target_record_frames = 0;
    track->count_in_beat_frames = 0;
}

static bool process_record_metronome_should_run(const synth_looper_process_track_t *tracks)
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

static bool process_track_recording(synth_looper_process_track_t *track,
                                    const int16_t *block,
                                    uint32_t frame,
                                    uint8_t channels)
{
    if (track->state != SYNTH_LOOPER_TRACK_RECORDING || track->samples == NULL) {
        return false;
    }
    if (track->record_position >= s_capacity_frames) {
        return true;
    }

    uint32_t sample_index = frame * channels;
    int32_t mono = block[sample_index];
    if (channels > 1) {
        mono = (mono + block[sample_index + 1]) / 2;
    }
    track->samples[track->record_position++] = encode_sample(mono);

    if (track->target_record_frames > 0 && track->record_position >= track->target_record_frames) {
        return true;
    }
    return track->record_position >= s_capacity_frames;
}

static bool process_track_playback(synth_looper_process_track_t *track,
                                   int32_t *mixed_sample)
{
    if (track->state != SYNTH_LOOPER_TRACK_LOOPING ||
        track->samples == NULL ||
        track->length_frames == 0) {
        return false;
    }

    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
    }

    uint32_t position = track->play_position;
    track->play_position = position + 1U;
    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
    }

    if (!track->audible || track->volume_percent == 0) {
        return false;
    }

    int32_t sample = decode_sample(track->samples[position]);
    uint32_t crossfade_frames = seam_crossfade_frames_for_length(track->length_frames);
    if (crossfade_frames > 0 && position >= track->length_frames - crossfade_frames) {
        uint32_t tail_offset = position - (track->length_frames - crossfade_frames);
        int32_t head = decode_sample(track->samples[0]);
        sample = synth_crossfade_smooth_i32(sample, head, tail_offset, crossfade_frames);
    }
    sample = apply_percent_gain(sample, track->volume_percent);
    *mixed_sample += sample;
    return true;
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
    synth_metronome_init(&s_global_metronome, sample_rate_hz);
    synth_metronome_init(&s_record_metronome, sample_rate_hz);
    clear_all_locked();
    s_initialized = true;
    ESP_RETURN_ON_ERROR(allocate_all_track_buffers(), TAG, "looper preallocation failed");
    set_message_locked("EMPTY");

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

void synth_looper_process_audio(int16_t *block, uint32_t frames, uint8_t channels)
{
    if (!s_initialized || block == NULL || frames == 0 || channels == 0) {
        return;
    }

    synth_looper_process_track_t process_tracks[SYNTH_LOOPER_TRACK_COUNT] = {0};
    bool started[SYNTH_LOOPER_TRACK_COUNT] = {0};
    bool completed[SYNTH_LOOPER_TRACK_COUNT] = {0};
    bool has_active_track = false;
    bool paused = false;
    bool record_metronome_enabled = false;
    uint16_t current_bpm = 120;
    synth_metronome_t global_metronome = {0};
    synth_metronome_t record_metronome = {0};
    uint32_t master_length_frames = 0;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    uint32_t live_peak = 0;
    uint32_t loop_peak = 0;
    uint32_t output_peak = 0;
    uint8_t max_audible_tracks = 0;
    synth_looper_meter_t block_meter = {0};
#endif

    portENTER_CRITICAL(&s_mux);
    uint64_t block_start = s_global_frame;
    s_global_frame += frames;
    paused = s_paused;
    current_bpm = s_current_bpm;
    record_metronome_enabled = s_record_metronome_enabled;
    global_metronome = s_global_metronome;
    record_metronome = s_record_metronome;
    master_length_frames = s_master_length_frames;
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        process_tracks[i].samples = s_tracks[i].samples;
        process_tracks[i].state = s_tracks[i].state;
        process_tracks[i].length_frames = s_tracks[i].length_frames;
        process_tracks[i].play_position = s_tracks[i].play_position;
        process_tracks[i].record_position = s_tracks[i].record_position;
        process_tracks[i].target_record_frames = s_tracks[i].target_record_frames;
        process_tracks[i].record_armed = s_tracks[i].record_armed;
        process_tracks[i].sync_to_master = s_tracks[i].sync_to_master;
        process_tracks[i].record_start_frame = s_tracks[i].record_start_frame;
        process_tracks[i].count_in_beat_frames = s_tracks[i].count_in_beat_frames;
        process_tracks[i].volume_percent = s_tracks[i].volume_percent;
        process_tracks[i].audible = track_is_audible(i);
        if (process_tracks[i].state == SYNTH_LOOPER_TRACK_RECORDING ||
            process_tracks[i].state == SYNTH_LOOPER_TRACK_LOOPING ||
            (process_tracks[i].state == SYNTH_LOOPER_TRACK_WAITING &&
             process_tracks[i].record_armed)) {
            has_active_track = true;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (paused ||
        (!has_active_track &&
         !synth_metronome_is_enabled(&global_metronome) &&
         !synth_metronome_is_enabled(&record_metronome))) {
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        portENTER_CRITICAL(&s_mux);
        memset(&s_meter, 0, sizeof(s_meter));
        portEXIT_CRITICAL(&s_mux);
#endif
        return;
    }

    uint32_t process_master_length = master_length_frames;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        uint64_t global_frame = block_start + frame;
        bool just_completed[SYNTH_LOOPER_TRACK_COUNT] = {0};
        bool master_cycle_start = process_master_length > 0 &&
                                  process_tracks[0].state == SYNTH_LOOPER_TRACK_LOOPING &&
                                  process_tracks[0].play_position == 0;
        int32_t click_mix = 0;
        int32_t loop_mix = 0;
        uint8_t active_playback_tracks = 0;

        click_mix = synth_metronome_sample(&global_metronome, global_frame);
        if (!synth_metronome_is_enabled(&global_metronome)) {
            click_mix += synth_metronome_sample(&record_metronome, global_frame);
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (process_track_should_start(&process_tracks[track], global_frame, master_cycle_start)) {
                start_process_recording(&process_tracks[track],
                                        &record_metronome,
                                        record_metronome_enabled,
                                        current_bpm,
                                        global_frame);
                started[track] = true;
            }
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (process_track_recording(&process_tracks[track], block, frame, channels)) {
                finish_process_recording(&process_tracks[track],
                                         track,
                                         true,
                                         process_master_length);
                completed[track] = true;
                just_completed[track] = true;
                if (track == 0) {
                    process_master_length = process_tracks[track].length_frames;
                }
            }
        }

        if (!process_record_metronome_should_run(process_tracks)) {
            synth_metronome_set_enabled(&record_metronome, false, current_bpm, global_frame);
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (just_completed[track]) {
                continue;
            }
            if (process_track_playback(&process_tracks[track], &loop_mix)) {
                ++active_playback_tracks;
            }
        }
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        if (active_playback_tracks > max_audible_tracks) {
            max_audible_tracks = active_playback_tracks;
        }
#endif

        if (active_playback_tracks > 0) {
            loop_mix = apply_gain_q8(loop_mix, loop_bus_gain_q8(active_playback_tracks));
        }
        loop_mix += click_mix;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
        meter_peak(&loop_peak, loop_mix);
#endif

        uint32_t sample_index = frame * channels;
        bool mix_path_active = active_playback_tracks > 0 || click_mix != 0;
        if (!mix_path_active) {
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            for (uint8_t channel = 0; channel < channels; ++channel) {
                int32_t live = block[sample_index + channel];
                meter_peak(&live_peak, live);
                meter_peak(&output_peak, live);
            }
#endif
            continue;
        }

        for (uint8_t channel = 0; channel < channels; ++channel) {
            int32_t live_input = block[sample_index + channel];
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            meter_peak(&live_peak, live_input);
#endif
            int32_t live = apply_gain_q8(live_input, LIVE_MIX_GAIN_Q8);
            int32_t mixed = live + loop_mix;
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
            block[sample_index + channel] = soft_limit_i16(mixed, &block_meter);
            meter_peak(&output_peak, block[sample_index + channel]);
#else
            block[sample_index + channel] = soft_limit_i16(mixed, NULL);
#endif
        }
    }

#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    block_meter.live_peak_percent = peak_to_percent(live_peak);
    block_meter.loop_peak_percent = peak_to_percent(loop_peak);
    block_meter.output_peak_percent = peak_to_percent(output_peak);
    log_meter_if_due(&block_meter, block_start, max_audible_tracks);
#endif

    portENTER_CRITICAL(&s_mux);
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    s_meter = block_meter;
#endif
    s_record_metronome = record_metronome;
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        synth_looper_track_t *slot = &s_tracks[track];
        if (slot->samples != process_tracks[track].samples) {
            continue;
        }
        if (slot->state == SYNTH_LOOPER_TRACK_LOOPING) {
            if (process_tracks[track].state == SYNTH_LOOPER_TRACK_LOOPING) {
                slot->play_position = process_tracks[track].play_position;
            }
        } else if (slot->state == SYNTH_LOOPER_TRACK_RECORDING) {
            if (completed[track] && process_tracks[track].state == SYNTH_LOOPER_TRACK_LOOPING) {
                slot->state = SYNTH_LOOPER_TRACK_LOOPING;
                slot->record_armed = false;
                slot->sync_to_master = false;
                slot->length_frames = process_tracks[track].length_frames;
                slot->play_position = process_tracks[track].play_position;
                slot->record_position = 0;
                slot->record_start_frame = 0;
                slot->target_record_frames = 0;
                slot->count_in_beat_frames = 0;
                if (s_master_length_frames == 0 || track == 0) {
                    s_master_length_frames = slot->length_frames;
                }
                s_current_track = next_empty_track_locked(track);
                set_message_locked("PLAY");
            } else if (process_tracks[track].state == SYNTH_LOOPER_TRACK_RECORDING) {
                slot->record_position = process_tracks[track].record_position;
            }
        } else if (slot->state == SYNTH_LOOPER_TRACK_WAITING && slot->record_armed && started[track]) {
            if (process_tracks[track].state == SYNTH_LOOPER_TRACK_RECORDING) {
                slot->state = SYNTH_LOOPER_TRACK_RECORDING;
                slot->record_armed = false;
                slot->sync_to_master = false;
                slot->record_position = process_tracks[track].record_position;
                slot->record_start_frame = 0;
                slot->count_in_beat_frames = 0;
                set_message_locked("REC");
            } else if (process_tracks[track].state == SYNTH_LOOPER_TRACK_LOOPING) {
                slot->state = SYNTH_LOOPER_TRACK_LOOPING;
                slot->record_armed = false;
                slot->sync_to_master = false;
                slot->length_frames = process_tracks[track].length_frames;
                slot->play_position = process_tracks[track].play_position;
                slot->record_position = 0;
                slot->record_start_frame = 0;
                slot->target_record_frames = 0;
                slot->count_in_beat_frames = 0;
                if (s_master_length_frames == 0 || track == 0) {
                    s_master_length_frames = slot->length_frames;
                }
                s_current_track = next_empty_track_locked(track);
                set_message_locked("PLAY");
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

esp_err_t synth_looper_click(uint16_t bpm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t track = s_current_track;
    esp_err_t err = ensure_track_buffer(track);
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_mux);
    set_current_bpm_locked(bpm);
    synth_looper_track_t *slot = &s_tracks[track];
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
        set_message_locked("WAITING");
        break;
    case SYNTH_LOOPER_TRACK_WAITING:
        arm_waiting_track_locked(track, bpm);
        break;
    case SYNTH_LOOPER_TRACK_RECORDING:
        finalize_recording_locked(track, true, false);
        break;
    case SYNTH_LOOPER_TRACK_LOOPING:
        if (track == 0) {
            clear_all_locked();
        } else {
            slot->state = SYNTH_LOOPER_TRACK_OFF;
            slot->play_position = 0;
            set_message_locked("OFF");
        }
        break;
    case SYNTH_LOOPER_TRACK_OFF:
        if (track == 0) {
            clear_all_locked();
        } else {
            clear_track_locked(track);
            set_message_locked("CLEARED");
        }
        break;
    default:
        break;
    }
    bool active = has_any_loop_locked();
    if (!active) {
        s_paused = false;
    }
    synth_looper_track_state_t state = slot->state;
    uint8_t bars = s_bar_count;
    bool paused = s_paused;
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG,
             "click track=%u state=%s bars=%u paused=%s",
             (unsigned)(track + 1),
             synth_looper_track_state_name(state),
             (unsigned)bars,
             paused ? "yes" : "no");
    return ESP_OK;
}

esp_err_t synth_looper_select_track(int8_t steps)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_mux);
    int value = (int)s_current_track + steps;
    while (value < 0) {
        value += SYNTH_LOOPER_TRACK_COUNT;
    }
    s_current_track = (uint8_t)(value % SYNTH_LOOPER_TRACK_COUNT);
    char message[sizeof(s_message)];
    snprintf(message, sizeof(message), "TRACK %u", (unsigned)(s_current_track + 1));
    set_message_locked(message);
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
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

    bool changed = false;
    uint8_t track_number = 0;
    uint8_t previous_percent = 0;
    synth_looper_track_state_t state = SYNTH_LOOPER_TRACK_EMPTY;

    portENTER_CRITICAL(&s_mux);
    synth_looper_track_t *slot = &s_tracks[track];
    s_current_track = track;
    track_number = (uint8_t)(track + 1U);
    previous_percent = slot->volume_percent;
    state = slot->state;
    if (slot->volume_percent != percent) {
        slot->volume_percent = percent;
        changed = true;
        char message[sizeof(s_message)];
        snprintf(message,
                 sizeof(message),
                 "T%u VOL %u",
                 (unsigned)track_number,
                 (unsigned)percent);
        set_message_locked(message);
    }
    portEXIT_CRITICAL(&s_mux);
    if (changed) {
        ESP_LOGI(TAG,
                 "track volume track=%u state=%s previous=%u%% current=%u%%",
                 (unsigned)track_number,
                 synth_looper_track_state_name(state),
                 (unsigned)previous_percent,
                 (unsigned)percent);
    }
    return ESP_OK;
}

esp_err_t synth_looper_set_current_track_volume(uint8_t percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t track = 0;
    portENTER_CRITICAL(&s_mux);
    track = s_current_track;
    portEXIT_CRITICAL(&s_mux);
    return synth_looper_set_track_volume(track, percent);
}

esp_err_t synth_looper_toggle_track_mute(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    bool muted = false;
    uint8_t track_number = (uint8_t)(track + 1U);
    portENTER_CRITICAL(&s_mux);
    uint8_t bit = track_bit(track);
    s_current_track = track;
    s_mute_mask ^= bit;
    muted = (s_mute_mask & bit) != 0;
    char message[sizeof(s_message)];
    snprintf(message, sizeof(message), "T%u %s", (unsigned)track_number, muted ? "MUTE" : "ON");
    set_message_locked(message);
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "track mute track=%u muted=%s", (unsigned)track_number, muted ? "yes" : "no");
    return ESP_OK;
}

esp_err_t synth_looper_toggle_track_solo(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    bool soloed = false;
    uint8_t solo_mask = 0;
    uint8_t track_number = (uint8_t)(track + 1U);
    portENTER_CRITICAL(&s_mux);
    uint8_t bit = track_bit(track);
    s_current_track = track;
    s_solo_mask ^= bit;
    soloed = (s_solo_mask & bit) != 0;
    solo_mask = s_solo_mask;
    char message[sizeof(s_message)];
    snprintf(message, sizeof(message), "T%u SOLO %s", (unsigned)track_number, soloed ? "ON" : "OFF");
    set_message_locked(message);
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG,
             "track solo track=%u soloed=%s solo_mask=0x%02x",
             (unsigned)track_number,
             soloed ? "yes" : "no",
             (unsigned)solo_mask);
    return ESP_OK;
}

esp_err_t synth_looper_toggle_metronome(uint16_t bpm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool enabled = false;
    portENTER_CRITICAL(&s_mux);
    set_current_bpm_locked(bpm);
    enabled = synth_metronome_toggle(&s_global_metronome, bpm, s_global_frame);
    set_message_locked(enabled ? "MET ON" : "MET OFF");
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "metronome=%s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_looper_toggle_record_metronome(uint16_t bpm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool enabled = false;
    portENTER_CRITICAL(&s_mux);
    set_current_bpm_locked(bpm);
    s_record_metronome_enabled = !s_record_metronome_enabled;
    enabled = s_record_metronome_enabled;
    if (!enabled) {
        synth_metronome_set_enabled(&s_record_metronome, false, s_current_bpm, s_global_frame);
    } else if (record_metronome_should_run_locked()) {
        synth_metronome_set_enabled(&s_record_metronome, true, s_current_bpm, s_global_frame);
    } else {
        synth_metronome_set_timing(&s_record_metronome, s_current_bpm, s_global_frame);
    }
    set_message_locked(enabled ? "REC MET ON" : "REC MET OFF");
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "record metronome=%s", enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t synth_looper_set_metronome_bpm(uint16_t bpm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_mux);
    set_current_bpm_locked(bpm);
    if (synth_metronome_is_enabled(&s_global_metronome)) {
        synth_metronome_set_bpm_preserve_phase(&s_global_metronome, s_current_bpm, s_global_frame);
    }
    if (synth_metronome_is_enabled(&s_record_metronome)) {
        synth_metronome_set_bpm_preserve_phase(&s_record_metronome, s_current_bpm, s_global_frame);
    } else if (s_record_metronome_enabled) {
        synth_metronome_set_timing(&s_record_metronome, s_current_bpm, s_global_frame);
    }
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

esp_err_t synth_looper_adjust_bars(int8_t steps)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_mux);
    int value = (int)s_bar_count + steps;
    if (value < 0) {
        value = 0;
    } else if (value > SYNTH_LOOPER_MAX_BARS) {
        value = SYNTH_LOOPER_MAX_BARS;
    }
    s_bar_count = (uint8_t)value;
    char message[sizeof(s_message)];
    if (s_bar_count == 0) {
        snprintf(message, sizeof(message), "FREE LENGTH");
    } else {
        snprintf(message, sizeof(message), "%u BARS", (unsigned)s_bar_count);
    }
    set_message_locked(message);
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

esp_err_t synth_looper_toggle_pause(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_mux);
    if (has_any_loop_locked()) {
        s_paused = !s_paused;
        set_message_locked(s_paused ? "PAUSED" : "PLAYING");
    } else {
        s_paused = false;
        set_message_locked("NO LOOP");
    }
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

esp_err_t synth_looper_clear_all(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_mux);
    clear_all_locked();
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "clear all");
    return ESP_OK;
}

esp_err_t synth_looper_clear_track(uint8_t track)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_mux);
    if (track == 0) {
        clear_all_locked();
    } else {
        clear_track_locked(track);
        s_current_track = track;
        if (!has_any_loop_locked()) {
            s_paused = false;
        }
        set_message_locked("CLEARED");
    }
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "clear track=%u", (unsigned)(track + 1));
    return ESP_OK;
}

esp_err_t synth_looper_auto_bounce(uint8_t bars, uint16_t bpm, uint8_t *track_out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t track = SYNTH_LOOPER_TRACK_COUNT;
    portENTER_CRITICAL(&s_mux);
    track = first_empty_track_locked();
    portEXIT_CRITICAL(&s_mux);
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ensure_track_buffer(track);
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_mux);
    if (s_tracks[track].state != SYNTH_LOOPER_TRACK_EMPTY) {
        track = first_empty_track_locked();
    }
    if (track >= SYNTH_LOOPER_TRACK_COUNT) {
        portEXIT_CRITICAL(&s_mux);
        return ESP_ERR_NOT_FOUND;
    }

    set_current_bpm_locked(bpm);
    s_current_track = track;

    synth_looper_track_t *slot = &s_tracks[track];
    if (s_master_length_frames > 0 && track != 0) {
        slot->state = SYNTH_LOOPER_TRACK_WAITING;
        slot->record_armed = true;
        slot->sync_to_master = true;
        slot->length_frames = 0;
        slot->play_position = 0;
        slot->record_position = 0;
        slot->record_start_frame = 0;
        slot->target_record_frames = s_master_length_frames;
        slot->count_in_beat_frames = 0;
        set_message_locked("BOUNCE SYNC");
    } else {
        uint32_t target_frames = bars_to_frames(bars == 0 ? 1 : bars, s_current_bpm);
        if (target_frames > s_capacity_frames) {
            target_frames = s_capacity_frames;
        }
        start_recording_locked(track, target_frames);
        set_message_locked("BOUNCE");
    }

    if (track_out != NULL) {
        *track_out = track;
    }
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "auto bounce track=%u bars=%u bpm=%u", (unsigned)(track + 1), (unsigned)bars, (unsigned)bpm);
    return ESP_OK;
}

bool synth_looper_current_track_is_waiting(void)
{
    bool waiting = false;
    if (!s_initialized) {
        return false;
    }

    portENTER_CRITICAL(&s_mux);
    waiting = s_tracks[s_current_track].state == SYNTH_LOOPER_TRACK_WAITING;
    portEXIT_CRITICAL(&s_mux);
    return waiting;
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

    portENTER_CRITICAL(&s_mux);
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        snapshot->track_state[i] = s_tracks[i].state;
        snapshot->track_ms[i] = frames_to_ms(s_tracks[i].length_frames);
        snapshot->track_position_ms[i] = frames_to_ms(s_tracks[i].play_position);
        snapshot->track_volume_percent[i] = s_tracks[i].volume_percent;
        snapshot->track_muted[i] = (s_mute_mask & track_bit(i)) != 0;
        snapshot->track_soloed[i] = (s_solo_mask & track_bit(i)) != 0;
        if (s_tracks[i].state == SYNTH_LOOPER_TRACK_RECORDING) {
            snapshot->track_ms[i] = frames_to_ms(s_tracks[i].record_position);
            snapshot->track_position_ms[i] = snapshot->track_ms[i];
        }
    }
#if ESP_POCKET_AUDIO_LOOPER_MIX_METER_ENABLED
    snapshot->meter = s_meter;
#endif
    snapshot->current_track = s_current_track;
    snapshot->page = s_current_track / SYNTH_LOOPER_TRACKS_PER_PAGE;
    snapshot->bar_count = s_bar_count;
    snapshot->paused = s_paused;
    snapshot->metronome_enabled = synth_metronome_is_enabled(&s_global_metronome);
    snapshot->record_metronome_enabled = s_record_metronome_enabled;
    snapshot->record_metronome_active = synth_metronome_is_enabled(&s_record_metronome);
    snapshot->solo_active = s_solo_mask != 0;
    snapshot->master_ms = frames_to_ms(s_master_length_frames);
    snapshot->max_seconds = SYNTH_LOOPER_MAX_SECONDS;
    const synth_looper_track_t *current = &s_tracks[s_current_track];
    snapshot->current_track_armed = current->record_armed;
    snapshot->waiting_for_sync = current->state == SYNTH_LOOPER_TRACK_WAITING &&
                                 current->record_armed &&
                                 current->sync_to_master;
    if (current->state == SYNTH_LOOPER_TRACK_WAITING && current->record_armed) {
        if (current->sync_to_master && s_master_length_frames > 0) {
            uint32_t master_position = s_tracks[0].play_position;
            if (master_position >= s_master_length_frames) {
                master_position = 0;
            }
            uint32_t wait_frames = master_position == 0 ? 0 : s_master_length_frames - master_position;
            snapshot->progress_ms = frames_to_ms(master_position);
            snapshot->progress_total_ms = frames_to_ms(s_master_length_frames);
            snapshot->wait_ms = frames_to_ms(wait_frames);
        } else if (!current->sync_to_master && current->record_start_frame > s_global_frame) {
            uint64_t wait_frames = current->record_start_frame - s_global_frame;
            uint64_t total_wait_frames = (uint64_t)current->count_in_beat_frames * 4ULL;
            snapshot->wait_ms = frames64_to_ms(wait_frames);
            snapshot->progress_total_ms = frames64_to_ms(total_wait_frames);
            if (total_wait_frames > wait_frames) {
                snapshot->progress_ms = frames64_to_ms(total_wait_frames - wait_frames);
            }
        }
    } else if (current->state == SYNTH_LOOPER_TRACK_RECORDING) {
        snapshot->progress_ms = frames_to_ms(current->record_position);
        if (current->target_record_frames > 0) {
            snapshot->progress_total_ms = frames_to_ms(current->target_record_frames);
        }
    } else if (current->state == SYNTH_LOOPER_TRACK_LOOPING && current->length_frames > 0) {
        snapshot->progress_ms = frames_to_ms(current->play_position);
        snapshot->progress_total_ms = frames_to_ms(current->length_frames);
    }
    strlcpy(snapshot->message, s_message, sizeof(snapshot->message));
    portEXIT_CRITICAL(&s_mux);
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
