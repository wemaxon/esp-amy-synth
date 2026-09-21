#include "synth/looper_view.h"

#include <stdio.h>
#include <string.h>

static uint32_t frames_to_ms(uint32_t sample_rate_hz, uint32_t frames)
{
    if (sample_rate_hz == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)frames * 1000ULL) / sample_rate_hz);
}

static uint32_t frames64_to_ms(uint32_t sample_rate_hz, uint64_t frames)
{
    if (sample_rate_hz == 0) {
        return 0;
    }
    uint64_t ms = (frames * 1000ULL) / sample_rate_hz;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static void format_message(const synth_looper_runtime_view_t *view, char *message, size_t size)
{
    switch (view->message_code) {
    case SYNTH_LOOPER_MESSAGE_NO_MEMORY:
        strlcpy(message, "NO MEMORY", size);
        break;
    case SYNTH_LOOPER_MESSAGE_CLEARED:
        strlcpy(message, "CLEARED", size);
        break;
    case SYNTH_LOOPER_MESSAGE_WAITING:
        strlcpy(message, "WAITING", size);
        break;
    case SYNTH_LOOPER_MESSAGE_SYNC_WAIT:
        strlcpy(message, "SYNC WAIT", size);
        break;
    case SYNTH_LOOPER_MESSAGE_COUNT_IN:
        strlcpy(message, "COUNT IN", size);
        break;
    case SYNTH_LOOPER_MESSAGE_RECORDING:
        strlcpy(message, "REC", size);
        break;
    case SYNTH_LOOPER_MESSAGE_PLAYING:
        strlcpy(message, "PLAY", size);
        break;
    case SYNTH_LOOPER_MESSAGE_TOO_SHORT:
        strlcpy(message, "TOO SHORT", size);
        break;
    case SYNTH_LOOPER_MESSAGE_OFF:
        strlcpy(message, "OFF", size);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK:
        snprintf(message, size, "TRACK %u", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK_VOLUME:
        snprintf(message,
                 size,
                 "T%u VOL %u",
                 (unsigned)view->message_arg0,
                 (unsigned)view->message_arg1);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK_MUTE:
        snprintf(message, size, "T%u MUTE", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK_ON:
        snprintf(message, size, "T%u ON", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK_SOLO_ON:
        snprintf(message, size, "T%u SOLO ON", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_TRACK_SOLO_OFF:
        snprintf(message, size, "T%u SOLO OFF", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_METRONOME_ON:
        strlcpy(message, "MET ON", size);
        break;
    case SYNTH_LOOPER_MESSAGE_METRONOME_OFF:
        strlcpy(message, "MET OFF", size);
        break;
    case SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_ON:
        strlcpy(message, "REC MET ON", size);
        break;
    case SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_OFF:
        strlcpy(message, "REC MET OFF", size);
        break;
    case SYNTH_LOOPER_MESSAGE_FREE_LENGTH:
        strlcpy(message, "FREE LENGTH", size);
        break;
    case SYNTH_LOOPER_MESSAGE_BARS:
        snprintf(message, size, "%u BARS", (unsigned)view->message_arg0);
        break;
    case SYNTH_LOOPER_MESSAGE_PAUSED:
        strlcpy(message, "PAUSED", size);
        break;
    case SYNTH_LOOPER_MESSAGE_RESUMED:
        strlcpy(message, "PLAYING", size);
        break;
    case SYNTH_LOOPER_MESSAGE_NO_LOOP:
        strlcpy(message, "NO LOOP", size);
        break;
    case SYNTH_LOOPER_MESSAGE_EMPTY:
    default:
        strlcpy(message, "EMPTY", size);
        break;
    }
}

void synth_looper_view_to_snapshot(uint32_t sample_rate_hz,
                                   const synth_looper_runtime_view_t *view,
                                     synth_looper_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    for (uint8_t track = 0; track < SYNTH_LOOPER_TRACK_COUNT; ++track) {
        snapshot->track_state[track] = view->track_state[track];
        snapshot->track_ms[track] = frames_to_ms(sample_rate_hz, view->track_length_frames[track]);
        snapshot->track_position_ms[track] = frames_to_ms(sample_rate_hz, view->track_position_frames[track]);
        snapshot->track_volume_percent[track] = view->track_volume_percent[track];
        snapshot->track_muted[track] = (view->mute_mask & (uint8_t)(1U << track)) != 0;
        snapshot->track_soloed[track] = (view->solo_mask & (uint8_t)(1U << track)) != 0;
        if (view->track_state[track] == SYNTH_LOOPER_TRACK_RECORDING) {
            snapshot->track_ms[track] = snapshot->track_position_ms[track];
        }
    }
    snapshot->meter = view->meter;
    snapshot->current_track = view->current_track;
    snapshot->page = view->current_track / SYNTH_LOOPER_TRACKS_PER_PAGE;
    snapshot->bar_count = view->bar_count;
    snapshot->paused = view->paused;
    snapshot->metronome_enabled = view->metronome_enabled;
    snapshot->record_metronome_enabled = view->record_metronome_enabled;
    snapshot->record_metronome_active = view->record_metronome_active;
    snapshot->solo_active = view->solo_mask != 0;
    snapshot->master_ms = frames_to_ms(sample_rate_hz, view->master_length_frames);
    snapshot->max_seconds = SYNTH_LOOPER_MAX_SECONDS;
    snapshot->current_track_armed = view->current_track_armed;
    snapshot->waiting_for_sync = view->waiting_for_sync;

    uint8_t current_track = view->current_track;
    synth_looper_track_state_t current_state = view->track_state[current_track];
    if (current_state == SYNTH_LOOPER_TRACK_WAITING && view->current_track_armed) {
        if (view->waiting_for_sync && view->master_length_frames > 0) {
            uint32_t master_position = view->track_position_frames[0];
            if (master_position >= view->master_length_frames) {
                master_position = 0;
            }
            uint32_t wait_frames = master_position == 0 ?
                0 : view->master_length_frames - master_position;
            snapshot->progress_ms = frames_to_ms(sample_rate_hz, master_position);
            snapshot->progress_total_ms = frames_to_ms(sample_rate_hz, view->master_length_frames);
            snapshot->wait_ms = frames_to_ms(sample_rate_hz, wait_frames);
        } else if (view->record_start_frame > view->global_frame) {
            uint64_t wait_frames = view->record_start_frame - view->global_frame;
            uint64_t total_wait_frames = (uint64_t)view->count_in_beat_frames * 4ULL;
            snapshot->wait_ms = frames64_to_ms(sample_rate_hz, wait_frames);
            snapshot->progress_total_ms = frames64_to_ms(sample_rate_hz, total_wait_frames);
            if (total_wait_frames > wait_frames) {
                snapshot->progress_ms = frames64_to_ms(sample_rate_hz, total_wait_frames - wait_frames);
            }
        }
    } else if (current_state == SYNTH_LOOPER_TRACK_RECORDING) {
        snapshot->progress_ms = frames_to_ms(sample_rate_hz, view->track_position_frames[current_track]);
        if (view->target_record_frames > 0) {
            snapshot->progress_total_ms = frames_to_ms(sample_rate_hz, view->target_record_frames);
        }
    } else if (current_state == SYNTH_LOOPER_TRACK_LOOPING &&
               view->track_length_frames[current_track] > 0) {
        snapshot->progress_ms = frames_to_ms(sample_rate_hz, view->track_position_frames[current_track]);
        snapshot->progress_total_ms = frames_to_ms(sample_rate_hz, view->track_length_frames[current_track]);
    }
    format_message(view, snapshot->message, sizeof(snapshot->message));
}
