#ifndef SYNTH_LOOPER_TRANSPORT_H
#define SYNTH_LOOPER_TRANSPORT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "synth/looper.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
#define SYNTH_LOOPER_COMMAND_CAPACITY 4U
#else
#define SYNTH_LOOPER_COMMAND_CAPACITY 32U
#endif
#define SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT 3U

#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
typedef uint32_t synth_looper_atomic_u32_t;
#else
typedef _Atomic uint32_t synth_looper_atomic_u32_t;
#endif

typedef enum {
    SYNTH_LOOPER_COMMAND_CLICK = 0,
    SYNTH_LOOPER_COMMAND_SELECT_TRACK,
    SYNTH_LOOPER_COMMAND_SET_TRACK_VOLUME,
    SYNTH_LOOPER_COMMAND_SET_CURRENT_TRACK_VOLUME,
    SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_MUTE,
    SYNTH_LOOPER_COMMAND_TOGGLE_TRACK_SOLO,
    SYNTH_LOOPER_COMMAND_TOGGLE_METRONOME,
    SYNTH_LOOPER_COMMAND_TOGGLE_RECORD_METRONOME,
    SYNTH_LOOPER_COMMAND_SET_METRONOME_BPM,
    SYNTH_LOOPER_COMMAND_ADJUST_BARS,
    SYNTH_LOOPER_COMMAND_TOGGLE_PAUSE,
    SYNTH_LOOPER_COMMAND_CLEAR_ALL,
    SYNTH_LOOPER_COMMAND_CLEAR_TRACK,
} synth_looper_command_type_t;

typedef struct {
    synth_looper_command_type_t type;
    uint32_t submitted_block;
    int16_t value;
    uint16_t bpm;
    uint8_t track;
} synth_looper_command_t;

typedef struct {
    synth_looper_command_t commands[SYNTH_LOOPER_COMMAND_CAPACITY];
    synth_looper_atomic_u32_t head;
    synth_looper_atomic_u32_t tail;
    synth_looper_atomic_u32_t high_water;
    synth_looper_atomic_u32_t rejected;
} synth_looper_command_queue_t;

/* The command queue is single-producer/single-consumer by contract. */

typedef enum {
    SYNTH_LOOPER_MESSAGE_EMPTY = 0,
    SYNTH_LOOPER_MESSAGE_NO_MEMORY,
    SYNTH_LOOPER_MESSAGE_CLEARED,
    SYNTH_LOOPER_MESSAGE_WAITING,
    SYNTH_LOOPER_MESSAGE_SYNC_WAIT,
    SYNTH_LOOPER_MESSAGE_COUNT_IN,
    SYNTH_LOOPER_MESSAGE_RECORDING,
    SYNTH_LOOPER_MESSAGE_PLAYING,
    SYNTH_LOOPER_MESSAGE_TOO_SHORT,
    SYNTH_LOOPER_MESSAGE_OFF,
    SYNTH_LOOPER_MESSAGE_TRACK,
    SYNTH_LOOPER_MESSAGE_TRACK_VOLUME,
    SYNTH_LOOPER_MESSAGE_TRACK_MUTE,
    SYNTH_LOOPER_MESSAGE_TRACK_ON,
    SYNTH_LOOPER_MESSAGE_TRACK_SOLO_ON,
    SYNTH_LOOPER_MESSAGE_TRACK_SOLO_OFF,
    SYNTH_LOOPER_MESSAGE_METRONOME_ON,
    SYNTH_LOOPER_MESSAGE_METRONOME_OFF,
    SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_ON,
    SYNTH_LOOPER_MESSAGE_RECORD_METRONOME_OFF,
    SYNTH_LOOPER_MESSAGE_FREE_LENGTH,
    SYNTH_LOOPER_MESSAGE_BARS,
    SYNTH_LOOPER_MESSAGE_PAUSED,
    SYNTH_LOOPER_MESSAGE_RESUMED,
    SYNTH_LOOPER_MESSAGE_NO_LOOP,
} synth_looper_message_code_t;

typedef struct {
    synth_looper_track_state_t track_state[SYNTH_LOOPER_TRACK_COUNT];
    uint32_t track_length_frames[SYNTH_LOOPER_TRACK_COUNT];
    uint32_t track_position_frames[SYNTH_LOOPER_TRACK_COUNT];
    uint8_t track_volume_percent[SYNTH_LOOPER_TRACK_COUNT];
    uint8_t mute_mask;
    uint8_t solo_mask;
    synth_looper_meter_t meter;
    uint64_t global_frame;
    uint32_t master_length_frames;
    uint32_t target_record_frames;
    uint64_t record_start_frame;
    uint32_t count_in_beat_frames;
    uint32_t audio_block;
    uint8_t current_track;
    uint8_t bar_count;
    synth_looper_message_code_t message_code;
    uint8_t message_arg0;
    uint8_t message_arg1;
    bool paused;
    bool metronome_enabled;
    bool record_metronome_enabled;
    bool record_metronome_active;
    bool current_track_armed;
    bool waiting_for_sync;
} synth_looper_runtime_view_t;

typedef struct {
    synth_looper_atomic_u32_t readers;
    synth_looper_runtime_view_t value;
} synth_looper_snapshot_slot_t;

typedef struct {
    synth_looper_snapshot_slot_t slots[SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT];
    synth_looper_atomic_u32_t published_slot;
    synth_looper_atomic_u32_t reader_retries;
    synth_looper_atomic_u32_t publish_skipped;
} synth_looper_snapshot_store_t;

void synth_looper_command_queue_init(synth_looper_command_queue_t *queue);
esp_err_t synth_looper_command_queue_push(synth_looper_command_queue_t *queue,
                                          const synth_looper_command_t *command);
bool synth_looper_command_queue_pop(synth_looper_command_queue_t *queue,
                                    synth_looper_command_t *command);
uint32_t synth_looper_command_queue_depth(const synth_looper_command_queue_t *queue);
uint32_t synth_looper_command_queue_high_water(const synth_looper_command_queue_t *queue);
uint32_t synth_looper_command_queue_rejected(const synth_looper_command_queue_t *queue);

void synth_looper_snapshot_store_init(synth_looper_snapshot_store_t *store,
                                      const synth_looper_runtime_view_t *initial);
bool synth_looper_snapshot_publish(synth_looper_snapshot_store_t *store,
                                   const synth_looper_runtime_view_t *view);
bool synth_looper_snapshot_read(synth_looper_snapshot_store_t *store,
                                synth_looper_runtime_view_t *view);
uint32_t synth_looper_snapshot_reader_retries(const synth_looper_snapshot_store_t *store);
uint32_t synth_looper_snapshot_publish_skipped(const synth_looper_snapshot_store_t *store);

#ifdef __cplusplus
}
#endif

#endif
