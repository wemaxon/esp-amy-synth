#include "synth/looper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

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
    int32_t declick_offset;
    uint16_t declick_remaining;
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
    int32_t declick_offset;
    uint16_t declick_remaining;
} synth_looper_process_track_t;

static const char *TAG = "SYNTH_LOOPER";

static const uint8_t LIVE_GAIN_NUM = 3;
static const uint8_t LIVE_GAIN_DEN = 4;
static const uint8_t LOOP_GAIN_NUM = 3;
static const uint8_t LOOP_GAIN_DEN = 4;
static const uint32_t MIN_RECORD_MS = 120;
static const uint16_t LOOP_DECLICK_FRAMES = 128;
static const uint16_t COUNT_IN_CLICK_FRAMES = 220;
static const int32_t COUNT_IN_CLICK_AMP = 4200;
static const int32_t COUNT_IN_CLICK_ACCENT_AMP = 6200;

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
static char s_message[22] = "EMPTY";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

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

static uint32_t count_in_frames(uint16_t bpm)
{
    if (bpm == 0) {
        bpm = 120;
    }
    return (uint32_t)((4ULL * 60ULL * (uint64_t)s_sample_rate_hz) / (uint64_t)bpm);
}

static uint32_t beat_frames(uint16_t bpm)
{
    if (bpm == 0) {
        bpm = 120;
    }
    return (uint32_t)((60ULL * (uint64_t)s_sample_rate_hz) / (uint64_t)bpm);
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
    slot->declick_offset = 0;
    slot->declick_remaining = 0;
}

static void clear_all_locked(void)
{
    for (uint8_t i = 0; i < SYNTH_LOOPER_TRACK_COUNT; ++i) {
        clear_track_locked(i);
    }
    s_master_length_frames = 0;
    s_paused = false;
    s_current_track = 0;
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
    return ((int32_t)sample * LOOP_GAIN_NUM) / LOOP_GAIN_DEN;
}

static uint16_t declick_frames_for_length(uint32_t length_frames)
{
    uint32_t frames = LOOP_DECLICK_FRAMES;
    uint32_t max_frames = length_frames / 4U;
    if (frames > max_frames) {
        frames = max_frames;
    }
    return (uint16_t)frames;
}

static int16_t clamp_i16(int32_t sample)
{
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return (int16_t)sample;
}

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
    slot->declick_offset = 0;
    slot->declick_remaining = 0;

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
    slot->declick_offset = 0;
    slot->declick_remaining = 0;
    slot->state = SYNTH_LOOPER_TRACK_LOOPING;

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
        slot->count_in_beat_frames = beat_frames(bpm);
        slot->record_start_frame = s_global_frame + count_in_frames(bpm);
        slot->target_record_frames = target_frames;
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

static void start_process_recording(synth_looper_process_track_t *track)
{
    track->state = SYNTH_LOOPER_TRACK_RECORDING;
    track->record_armed = false;
    track->sync_to_master = false;
    track->length_frames = 0;
    track->play_position = 0;
    track->record_position = 0;
    track->record_start_frame = 0;
    track->count_in_beat_frames = 0;
    track->declick_offset = 0;
    track->declick_remaining = 0;
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
    track->declick_offset = 0;
    track->declick_remaining = 0;
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

static int32_t count_in_click_sample(const synth_looper_process_track_t *track,
                                     uint64_t global_frame)
{
    if (track->state != SYNTH_LOOPER_TRACK_WAITING ||
        !track->record_armed ||
        track->sync_to_master ||
        track->count_in_beat_frames == 0 ||
        global_frame >= track->record_start_frame) {
        return 0;
    }

    uint64_t count_in_length = (uint64_t)track->count_in_beat_frames * 4ULL;
    if (track->record_start_frame < count_in_length) {
        return 0;
    }
    uint64_t count_in_start = track->record_start_frame - count_in_length;
    if (global_frame < count_in_start) {
        return 0;
    }

    uint64_t count_in_offset = global_frame - count_in_start;
    uint32_t beat_offset = (uint32_t)(count_in_offset % track->count_in_beat_frames);
    if (beat_offset >= COUNT_IN_CLICK_FRAMES) {
        return 0;
    }

    uint32_t beat_index = (uint32_t)(count_in_offset / track->count_in_beat_frames);
    int32_t amplitude = beat_index == 0 ? COUNT_IN_CLICK_ACCENT_AMP : COUNT_IN_CLICK_AMP;
    int32_t envelope = (amplitude * (int32_t)(COUNT_IN_CLICK_FRAMES - beat_offset)) /
                       (int32_t)COUNT_IN_CLICK_FRAMES;
    return ((beat_offset / 8U) & 1U) != 0 ? -envelope : envelope;
}

static void process_track_playback(synth_looper_process_track_t *track,
                                   int32_t *mixed_sample)
{
    if (track->state != SYNTH_LOOPER_TRACK_LOOPING ||
        track->samples == NULL ||
        track->length_frames == 0) {
        return;
    }

    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
    }

    uint32_t position = track->play_position;
    int32_t sample = decode_sample(track->samples[position]);
    bool wrapped = false;

    track->play_position = position + 1U;
    if (track->play_position >= track->length_frames) {
        track->play_position = 0;
        wrapped = true;
    }

    uint16_t declick_frames = declick_frames_for_length(track->length_frames);
    if (declick_frames > 0 && track->declick_remaining > 0) {
        sample += (track->declick_offset * (int32_t)track->declick_remaining) /
                  (int32_t)declick_frames;
        --track->declick_remaining;
        if (track->declick_remaining == 0) {
            track->declick_offset = 0;
        }
    }

    if (wrapped && declick_frames > 0) {
        int32_t next_sample = decode_sample(track->samples[0]);
        track->declick_offset = sample - next_sample;
        track->declick_remaining = declick_frames;
    }
    *mixed_sample += sample;
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
    clear_all_locked();
    s_initialized = true;
    set_message_locked("EMPTY");

    ESP_LOGI(TAG,
             "looper ready tracks=%u max_seconds=%u sample_rate=%lu channels=%u storage=mono-int16 bytes_per_track=%lu",
             (unsigned)SYNTH_LOOPER_TRACK_COUNT,
             (unsigned)SYNTH_LOOPER_MAX_SECONDS,
             (unsigned long)s_sample_rate_hz,
             (unsigned)s_channel_count,
             (unsigned long)track_buffer_bytes());
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
    uint32_t master_length_frames = 0;

    portENTER_CRITICAL(&s_mux);
    uint64_t block_start = s_global_frame;
    s_global_frame += frames;
    paused = s_paused;
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
        process_tracks[i].declick_offset = s_tracks[i].declick_offset;
        process_tracks[i].declick_remaining = s_tracks[i].declick_remaining;
        if (process_tracks[i].state == SYNTH_LOOPER_TRACK_RECORDING ||
            process_tracks[i].state == SYNTH_LOOPER_TRACK_LOOPING ||
            (process_tracks[i].state == SYNTH_LOOPER_TRACK_WAITING &&
             process_tracks[i].record_armed)) {
            has_active_track = true;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (paused || !has_active_track) {
        return;
    }

    uint32_t process_master_length = master_length_frames;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        uint64_t global_frame = block_start + frame;
        bool just_completed[SYNTH_LOOPER_TRACK_COUNT] = {0};
        bool master_cycle_start = process_master_length > 0 &&
                                  process_tracks[0].state == SYNTH_LOOPER_TRACK_LOOPING &&
                                  process_tracks[0].play_position == 0;
        int32_t loop_mix = 0;

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            loop_mix += count_in_click_sample(&process_tracks[track], global_frame);
        }

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (process_track_should_start(&process_tracks[track], global_frame, master_cycle_start)) {
                start_process_recording(&process_tracks[track]);
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

        for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
            if (just_completed[track]) {
                continue;
            }
            process_track_playback(&process_tracks[track], &loop_mix);
        }

        if (loop_mix == 0) {
            continue;
        }

        uint32_t sample_index = frame * channels;
        for (uint8_t channel = 0; channel < channels; ++channel) {
            int32_t live = ((int32_t)block[sample_index + channel] * LIVE_GAIN_NUM) / LIVE_GAIN_DEN;
            int32_t mixed = live + loop_mix;
            block[sample_index + channel] = clamp_i16(mixed);
        }
    }

    portENTER_CRITICAL(&s_mux);
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        synth_looper_track_t *slot = &s_tracks[track];
        if (slot->samples != process_tracks[track].samples) {
            continue;
        }
        if (slot->state == SYNTH_LOOPER_TRACK_LOOPING) {
            if (process_tracks[track].state == SYNTH_LOOPER_TRACK_LOOPING) {
                slot->play_position = process_tracks[track].play_position;
                slot->declick_offset = process_tracks[track].declick_offset;
                slot->declick_remaining = process_tracks[track].declick_remaining;
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
                slot->declick_offset = process_tracks[track].declick_offset;
                slot->declick_remaining = process_tracks[track].declick_remaining;
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
                slot->declick_offset = 0;
                slot->declick_remaining = 0;
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
                slot->declick_offset = process_tracks[track].declick_offset;
                slot->declick_remaining = process_tracks[track].declick_remaining;
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
        if (s_tracks[i].state == SYNTH_LOOPER_TRACK_RECORDING) {
            snapshot->track_ms[i] = frames_to_ms(s_tracks[i].record_position);
            snapshot->track_position_ms[i] = snapshot->track_ms[i];
        }
    }
    snapshot->current_track = s_current_track;
    snapshot->page = s_current_track / SYNTH_LOOPER_TRACKS_PER_PAGE;
    snapshot->bar_count = s_bar_count;
    snapshot->paused = s_paused;
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
