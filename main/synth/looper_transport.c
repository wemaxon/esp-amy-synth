#include "synth/looper_transport.h"

#include <stddef.h>

_Static_assert((SYNTH_LOOPER_COMMAND_CAPACITY & (SYNTH_LOOPER_COMMAND_CAPACITY - 1U)) == 0,
               "looper command capacity must be a power of two");

static void atomic_value_init(synth_looper_atomic_u32_t *value, uint32_t initial)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    *value = initial;
#else
    atomic_init(value, initial);
#endif
}

static uint32_t atomic_value_load_relaxed(const synth_looper_atomic_u32_t *value)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    return *value;
#else
    return atomic_load_explicit(value, memory_order_relaxed);
#endif
}

static uint32_t atomic_value_load_acquire(const synth_looper_atomic_u32_t *value)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    return *value;
#else
    return atomic_load_explicit(value, memory_order_acquire);
#endif
}

static void atomic_value_store_release(synth_looper_atomic_u32_t *value, uint32_t next)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    *value = next;
#else
    atomic_store_explicit(value, next, memory_order_release);
#endif
}

static uint32_t atomic_value_fetch_add(synth_looper_atomic_u32_t *value, uint32_t amount)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    uint32_t previous = *value;
    *value += amount;
    return previous;
#else
    return atomic_fetch_add_explicit(value, amount, memory_order_acq_rel);
#endif
}

static uint32_t atomic_value_fetch_sub(synth_looper_atomic_u32_t *value, uint32_t amount)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    uint32_t previous = *value;
    *value -= amount;
    return previous;
#else
    return atomic_fetch_sub_explicit(value, amount, memory_order_acq_rel);
#endif
}

static bool atomic_value_compare_exchange_weak(synth_looper_atomic_u32_t *value,
                                               uint32_t *expected,
                                               uint32_t desired)
{
#if defined(SYNTH_LOOPER_TRANSPORT_SINGLE_THREADED_TEST)
    if (*value == *expected) {
        *value = desired;
        return true;
    }
    *expected = *value;
    return false;
#else
    return atomic_compare_exchange_weak_explicit(value,
                                                 expected,
                                                 desired,
                                                 memory_order_relaxed,
                                                 memory_order_relaxed);
#endif
}

static void update_max(synth_looper_atomic_u32_t *maximum, uint32_t value)
{
    uint32_t current = atomic_value_load_relaxed(maximum);
    while (current < value &&
           !atomic_value_compare_exchange_weak(maximum, &current, value)) {
    }
}

void synth_looper_command_queue_init(synth_looper_command_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    atomic_value_init(&queue->head, 0);
    atomic_value_init(&queue->tail, 0);
    atomic_value_init(&queue->high_water, 0);
    atomic_value_init(&queue->rejected, 0);
}

esp_err_t synth_looper_command_queue_push(synth_looper_command_queue_t *queue,
                                          const synth_looper_command_t *command)
{
    if (queue == NULL || command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t head = atomic_value_load_relaxed(&queue->head);
    uint32_t tail = atomic_value_load_acquire(&queue->tail);
    uint32_t depth = head - tail;
    if (depth >= SYNTH_LOOPER_COMMAND_CAPACITY) {
        atomic_value_fetch_add(&queue->rejected, 1);
        return ESP_ERR_NO_MEM;
    }

    queue->commands[head & (SYNTH_LOOPER_COMMAND_CAPACITY - 1U)] = *command;
    atomic_value_store_release(&queue->head, head + 1U);
    update_max(&queue->high_water, depth + 1U);
    return ESP_OK;
}

bool synth_looper_command_queue_pop(synth_looper_command_queue_t *queue,
                                    synth_looper_command_t *command)
{
    if (queue == NULL || command == NULL) {
        return false;
    }

    uint32_t tail = atomic_value_load_relaxed(&queue->tail);
    uint32_t head = atomic_value_load_acquire(&queue->head);
    if (tail == head) {
        return false;
    }

    *command = queue->commands[tail & (SYNTH_LOOPER_COMMAND_CAPACITY - 1U)];
    atomic_value_store_release(&queue->tail, tail + 1U);
    return true;
}

uint32_t synth_looper_command_queue_depth(const synth_looper_command_queue_t *queue)
{
    if (queue == NULL) {
        return 0;
    }
    uint32_t head = atomic_value_load_acquire(&queue->head);
    uint32_t tail = atomic_value_load_acquire(&queue->tail);
    return head - tail;
}

uint32_t synth_looper_command_queue_high_water(const synth_looper_command_queue_t *queue)
{
    return queue == NULL ? 0 : atomic_value_load_relaxed(&queue->high_water);
}

uint32_t synth_looper_command_queue_rejected(const synth_looper_command_queue_t *queue)
{
    return queue == NULL ? 0 : atomic_value_load_relaxed(&queue->rejected);
}

void synth_looper_snapshot_store_init(synth_looper_snapshot_store_t *store,
                                      const synth_looper_runtime_view_t *initial)
{
    if (store == NULL || initial == NULL) {
        return;
    }
    for (uint32_t i = 0; i < SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT; ++i) {
        atomic_value_init(&store->slots[i].readers, 0);
        store->slots[i].value = *initial;
    }
    atomic_value_init(&store->published_slot, 0);
    atomic_value_init(&store->reader_retries, 0);
    atomic_value_init(&store->publish_skipped, 0);
}

bool synth_looper_snapshot_publish(synth_looper_snapshot_store_t *store,
                                   const synth_looper_runtime_view_t *view)
{
    if (store == NULL || view == NULL) {
        return false;
    }

    uint32_t published = atomic_value_load_acquire(&store->published_slot);
    for (uint32_t offset = 1; offset < SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT; ++offset) {
        uint32_t slot = (published + offset) % SYNTH_LOOPER_SNAPSHOT_SLOT_COUNT;
        if (atomic_value_load_acquire(&store->slots[slot].readers) != 0) {
            continue;
        }
        store->slots[slot].value = *view;
        atomic_value_store_release(&store->published_slot, slot);
        return true;
    }

    atomic_value_fetch_add(&store->publish_skipped, 1);
    return false;
}

bool synth_looper_snapshot_read(synth_looper_snapshot_store_t *store,
                                synth_looper_runtime_view_t *view)
{
    if (store == NULL || view == NULL) {
        return false;
    }

    for (;;) {
        uint32_t slot = atomic_value_load_acquire(&store->published_slot);
        atomic_value_fetch_add(&store->slots[slot].readers, 1);
        if (slot != atomic_value_load_acquire(&store->published_slot)) {
            atomic_value_fetch_sub(&store->slots[slot].readers, 1);
            atomic_value_fetch_add(&store->reader_retries, 1);
            continue;
        }

        *view = store->slots[slot].value;
        atomic_value_fetch_sub(&store->slots[slot].readers, 1);
        return true;
    }
}

uint32_t synth_looper_snapshot_reader_retries(const synth_looper_snapshot_store_t *store)
{
    return store == NULL ? 0 : atomic_value_load_relaxed(&store->reader_retries);
}

uint32_t synth_looper_snapshot_publish_skipped(const synth_looper_snapshot_store_t *store)
{
    return store == NULL ? 0 : atomic_value_load_relaxed(&store->publish_skipped);
}
