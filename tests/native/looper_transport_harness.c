#include "test_check.h"
#include <stdint.h>

#include "synth/looper_transport.h"

int32_t host_vector_looper_command_fifo(void)
{
    synth_looper_command_queue_t queue;
    synth_looper_command_queue_init(&queue);
    for (uint32_t i = 0; i < 3; ++i) {
        synth_looper_command_t command = {
            .type = SYNTH_LOOPER_COMMAND_SET_TRACK_VOLUME,
            .value = (int16_t)(i + 10U),
        };
        if (synth_looper_command_queue_push(&queue, &command) != ESP_OK) {
            return -1;
        }
    }

    int32_t result = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        synth_looper_command_t command = {0};
        if (!synth_looper_command_queue_pop(&queue, &command)) {
            return -2;
        }
        result = result * 10 + (int32_t)(command.value - 10);
    }
    return result;
}

int32_t host_vector_looper_command_full(void)
{
    synth_looper_command_queue_t queue;
    synth_looper_command_queue_init(&queue);
    synth_looper_command_t command = {0};
    for (uint32_t i = 0; i < SYNTH_LOOPER_COMMAND_CAPACITY; ++i) {
        command.value = (int16_t)i;
        if (synth_looper_command_queue_push(&queue, &command) != ESP_OK) {
            return -1;
        }
    }
    return synth_looper_command_queue_push(&queue, &command);
}

int32_t host_vector_looper_command_wrap(void)
{
    synth_looper_command_queue_t queue;
    synth_looper_command_queue_init(&queue);
    synth_looper_command_t command = {0};
    for (uint32_t cycle = 0; cycle < 4; ++cycle) {
        for (uint32_t i = 0; i < SYNTH_LOOPER_COMMAND_CAPACITY; ++i) {
            command.value = (int16_t)(cycle * SYNTH_LOOPER_COMMAND_CAPACITY + i);
            if (synth_looper_command_queue_push(&queue, &command) != ESP_OK) {
                return -1;
            }
        }
        for (uint32_t i = 0; i < SYNTH_LOOPER_COMMAND_CAPACITY; ++i) {
            if (!synth_looper_command_queue_pop(&queue, &command)) {
                return -2;
            }
        }
    }
    return command.value;
}

int32_t host_vector_looper_command_high_water(void)
{
    synth_looper_command_queue_t queue;
    synth_looper_command_queue_init(&queue);
    synth_looper_command_t command = {0};
    for (uint32_t i = 0; i < 3; ++i) {
        synth_looper_command_queue_push(&queue, &command);
    }
    return (int32_t)synth_looper_command_queue_high_water(&queue);
}

int32_t host_vector_looper_command_rejected(void)
{
    synth_looper_command_queue_t queue;
    synth_looper_command_queue_init(&queue);
    synth_looper_command_t command = {0};
    for (uint32_t i = 0; i < SYNTH_LOOPER_COMMAND_CAPACITY; ++i) {
        if (synth_looper_command_queue_push(&queue, &command) != ESP_OK) {
            return -1;
        }
    }
    if (synth_looper_command_queue_push(&queue, &command) != ESP_ERR_NO_MEM) {
        return -2;
    }
    return (int32_t)(synth_looper_command_queue_rejected(&queue) * 100U +
                     synth_looper_command_queue_depth(&queue));
}

int32_t host_vector_looper_snapshot_consistent(void)
{
    synth_looper_snapshot_store_t store;
    synth_looper_runtime_view_t initial = {
        .audio_block = 1,
        .current_track = 2,
    };
    synth_looper_snapshot_store_init(&store, &initial);

    synth_looper_runtime_view_t next = initial;
    next.audio_block = 2;
    next.current_track = 4;
    if (!synth_looper_snapshot_publish(&store, &next)) {
        return -1;
    }

    synth_looper_runtime_view_t read = {0};
    if (!synth_looper_snapshot_read(&store, &read)) {
        return -2;
    }
    return (int32_t)(read.audio_block * 10U + read.current_track);
}

int32_t host_vector_looper_snapshot_pinned(void)
{
    synth_looper_snapshot_store_t store;
    synth_looper_runtime_view_t view = {.audio_block = 1};
    synth_looper_snapshot_store_init(&store, &view);
    uint32_t first = store.published_slot;
    store.slots[first].readers = 1;

    view.audio_block = 2;
    if (!synth_looper_snapshot_publish(&store, &view)) {
        return -1;
    }
    view.audio_block = 3;
    if (!synth_looper_snapshot_publish(&store, &view)) {
        return -2;
    }
    return (int32_t)store.slots[first].value.audio_block;
}

int32_t host_vector_looper_snapshot_skipped(void)
{
    synth_looper_snapshot_store_t store;
    synth_looper_runtime_view_t view = {.audio_block = 7};
    synth_looper_snapshot_store_init(&store, &view);
    store.slots[1].readers = 1;
    store.slots[2].readers = 1;

    view.audio_block = 8;
    if (synth_looper_snapshot_publish(&store, &view)) {
        return -1;
    }
    return (int32_t)(synth_looper_snapshot_publish_skipped(&store) * 10U +
                     store.slots[0].value.audio_block);
}

int main(void)
{
    CHECK_VECTOR(host_vector_looper_command_fifo, 12);
    CHECK_VECTOR(host_vector_looper_command_full, 257);
    CHECK_VECTOR(host_vector_looper_command_wrap, 4 * SYNTH_LOOPER_COMMAND_CAPACITY - 1);
    CHECK_VECTOR(host_vector_looper_command_high_water, 3);
    CHECK_VECTOR(host_vector_looper_command_rejected, 100 + SYNTH_LOOPER_COMMAND_CAPACITY);
    CHECK_VECTOR(host_vector_looper_snapshot_consistent, 24);
    CHECK_VECTOR(host_vector_looper_snapshot_pinned, 1);
    CHECK_VECTOR(host_vector_looper_snapshot_skipped, 17);
    return 0;
}
