#include "synth/audio_meter.h"

#include <math.h>
#include <stddef.h>
#include <stdatomic.h>

#if !defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
#include "esp_timer.h"
#endif

#define AUDIO_METER_COMMAND_CAPACITY 4U
#define AUDIO_METER_SNAPSHOT_SLOT_COUNT 3U
#define AUDIO_METER_MOMENTARY_BLOCK_CAPACITY 128U
#define AUDIO_METER_MAX_CHANNELS 2U

_Static_assert((AUDIO_METER_COMMAND_CAPACITY & (AUDIO_METER_COMMAND_CAPACITY - 1U)) == 0,
               "audio meter command capacity must be a power of two");

#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
typedef uint32_t audio_meter_atomic_u32_t;
#else
typedef _Atomic uint32_t audio_meter_atomic_u32_t;
#endif

typedef struct {
    synth_audio_meter_request_t request[AUDIO_METER_COMMAND_CAPACITY];
    audio_meter_atomic_u32_t head;
    audio_meter_atomic_u32_t tail;
} audio_meter_command_queue_t;

typedef struct {
    audio_meter_atomic_u32_t readers;
    synth_audio_meter_result_t value;
} audio_meter_snapshot_slot_t;

typedef struct {
    audio_meter_snapshot_slot_t slot[AUDIO_METER_SNAPSHOT_SLOT_COUNT];
    audio_meter_atomic_u32_t published;
} audio_meter_snapshot_store_t;

typedef struct {
    float z1;
    float z2;
} audio_meter_biquad_state_t;

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} audio_meter_biquad_coefficients_t;

typedef struct {
    audio_meter_biquad_state_t shelf;
    audio_meter_biquad_state_t high_pass;
} audio_meter_k_state_t;

typedef struct {
    float energy;
    uint32_t frames;
} audio_meter_energy_block_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t audio_block_frames;
    uint32_t audio_block_budget_us;
    uint32_t momentary_frames;
    audio_meter_command_queue_t commands;
    audio_meter_atomic_u32_t request_outstanding;
    audio_meter_snapshot_store_t snapshots;
    synth_audio_meter_request_t pending;
    synth_audio_meter_request_t active_request;
    bool has_pending;
    bool stop_invoked;
    synth_audio_meter_result_t result;
    audio_meter_biquad_coefficients_t k_shelf;
    audio_meter_biquad_coefficients_t k_high_pass;
    audio_meter_k_state_t k_state[AUDIO_METER_MAX_CHANNELS];
    float square_sum;
    float k_energy_sum;
    audio_meter_energy_block_t momentary[AUDIO_METER_MOMENTARY_BLOCK_CAPACITY];
    uint32_t momentary_head;
    uint32_t momentary_count;
    uint32_t momentary_frame_count;
    float momentary_energy_sum;
    bool initialized;
} audio_meter_state_t;

static audio_meter_state_t s_meter;

static void atomic_init_u32(audio_meter_atomic_u32_t *value, uint32_t initial)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    *value = initial;
#else
    atomic_init(value, initial);
#endif
}

static uint32_t atomic_load_relaxed_u32(const audio_meter_atomic_u32_t *value)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    return *value;
#else
    return atomic_load_explicit(value, memory_order_relaxed);
#endif
}

static uint32_t atomic_load_acquire_u32(const audio_meter_atomic_u32_t *value)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    return *value;
#else
    return atomic_load_explicit(value, memory_order_acquire);
#endif
}

static void atomic_store_release_u32(audio_meter_atomic_u32_t *value, uint32_t next)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    *value = next;
#else
    atomic_store_explicit(value, next, memory_order_release);
#endif
}

static uint32_t atomic_fetch_add_u32(audio_meter_atomic_u32_t *value, uint32_t amount)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    uint32_t previous = *value;
    *value += amount;
    return previous;
#else
    return atomic_fetch_add_explicit(value, amount, memory_order_acq_rel);
#endif
}

static uint32_t atomic_fetch_sub_u32(audio_meter_atomic_u32_t *value, uint32_t amount)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    uint32_t previous = *value;
    *value -= amount;
    return previous;
#else
    return atomic_fetch_sub_explicit(value, amount, memory_order_acq_rel);
#endif
}

static bool atomic_compare_exchange_u32(audio_meter_atomic_u32_t *value,
                                        uint32_t *expected,
                                        uint32_t desired)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    if (*value == *expected) {
        *value = desired;
        return true;
    }
    *expected = *value;
    return false;
#else
    return atomic_compare_exchange_strong_explicit(value,
                                                   expected,
                                                   desired,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire);
#endif
}

static bool command_push(const synth_audio_meter_request_t *request)
{
    uint32_t head = atomic_load_relaxed_u32(&s_meter.commands.head);
    uint32_t tail = atomic_load_acquire_u32(&s_meter.commands.tail);
    if (head - tail >= AUDIO_METER_COMMAND_CAPACITY) {
        return false;
    }
    s_meter.commands.request[head & (AUDIO_METER_COMMAND_CAPACITY - 1U)] = *request;
    atomic_store_release_u32(&s_meter.commands.head, head + 1U);
    return true;
}

static bool command_pop(synth_audio_meter_request_t *request)
{
    uint32_t tail = atomic_load_relaxed_u32(&s_meter.commands.tail);
    uint32_t head = atomic_load_acquire_u32(&s_meter.commands.head);
    if (tail == head) {
        return false;
    }
    *request = s_meter.commands.request[tail & (AUDIO_METER_COMMAND_CAPACITY - 1U)];
    atomic_store_release_u32(&s_meter.commands.tail, tail + 1U);
    return true;
}

static bool snapshot_publish(const synth_audio_meter_result_t *result)
{
    uint32_t published = atomic_load_acquire_u32(&s_meter.snapshots.published);
    for (uint32_t offset = 1; offset < AUDIO_METER_SNAPSHOT_SLOT_COUNT; ++offset) {
        uint32_t slot = (published + offset) % AUDIO_METER_SNAPSHOT_SLOT_COUNT;
        if (atomic_load_acquire_u32(&s_meter.snapshots.slot[slot].readers) != 0) {
            continue;
        }
        s_meter.snapshots.slot[slot].value = *result;
        atomic_store_release_u32(&s_meter.snapshots.published, slot);
        return true;
    }
    return false;
}

static float biquad_process(float input,
                            audio_meter_biquad_state_t *state,
                            const audio_meter_biquad_coefficients_t *coefficients)
{
    float output = coefficients->b0 * input + state->z1;
    state->z1 = coefficients->b1 * input - coefficients->a1 * output + state->z2;
    state->z2 = coefficients->b2 * input - coefficients->a2 * output;
    return output;
}

static float k_weight_sample(
    float input,
    audio_meter_k_state_t *state,
    const audio_meter_biquad_coefficients_t *shelf_coefficients,
    const audio_meter_biquad_coefficients_t *high_pass_coefficients)
{
    float shelf = biquad_process(input, &state->shelf, shelf_coefficients);
    return biquad_process(shelf, &state->high_pass, high_pass_coefficients);
}

static bool calculate_k_weighting_coefficients(
    uint32_t sample_rate_hz,
    audio_meter_biquad_coefficients_t *shelf,
    audio_meter_biquad_coefficients_t *high_pass)
{
    /* ITU-R BS.1770 parameters, transformed for the actual stream rate. */
    static const double PI = 3.14159265358979323846;
    static const double SHELF_F0_HZ = 1681.974450955533;
    static const double SHELF_GAIN_DB = 3.999843853973347;
    static const double SHELF_Q = 0.7071752369554196;
    static const double SHELF_VB_EXPONENT = 0.4996667741545416;
    static const double HIGH_PASS_F0_HZ = 38.13547087602444;
    static const double HIGH_PASS_Q = 0.5003270373238773;

    if (sample_rate_hz < 8000U || sample_rate_hz > 384000U ||
        shelf == NULL || high_pass == NULL) {
        return false;
    }

    double k = tan(PI * SHELF_F0_HZ / (double)sample_rate_hz);
    double vh = pow(10.0, SHELF_GAIN_DB / 20.0);
    double vb = pow(vh, SHELF_VB_EXPONENT);
    double a0 = 1.0 + k / SHELF_Q + k * k;
    *shelf = (audio_meter_biquad_coefficients_t){
        .b0 = (float)((vh + vb * k / SHELF_Q + k * k) / a0),
        .b1 = (float)(2.0 * (k * k - vh) / a0),
        .b2 = (float)((vh - vb * k / SHELF_Q + k * k) / a0),
        .a1 = (float)(2.0 * (k * k - 1.0) / a0),
        .a2 = (float)((1.0 - k / SHELF_Q + k * k) / a0),
    };

    k = tan(PI * HIGH_PASS_F0_HZ / (double)sample_rate_hz);
    a0 = 1.0 + k / HIGH_PASS_Q + k * k;
    *high_pass = (audio_meter_biquad_coefficients_t){
        .b0 = 1.0f,
        .b1 = -2.0f,
        .b2 = 1.0f,
        .a1 = (float)(2.0 * (k * k - 1.0) / a0),
        .a2 = (float)((1.0 - k / HIGH_PASS_Q + k * k) / a0),
    };
    return true;
}

static void reset_accumulators(const synth_audio_meter_request_t *request)
{
    s_meter.active_request = *request;
    s_meter.stop_invoked = request->on_stop == NULL;
    s_meter.result = (synth_audio_meter_result_t){
        .session_id = request->session_id,
        .tap = request->tap,
        .sample_rate_hz = s_meter.sample_rate_hz,
        .target_frames = request->target_frames,
        .active = true,
    };
    s_meter.square_sum = 0.0f;
    s_meter.k_energy_sum = 0.0f;
    s_meter.momentary_head = 0;
    s_meter.momentary_count = 0;
    s_meter.momentary_frame_count = 0;
    s_meter.momentary_energy_sum = 0.0f;
    for (uint32_t channel = 0; channel < AUDIO_METER_MAX_CHANNELS; ++channel) {
        s_meter.k_state[channel] = (audio_meter_k_state_t){0};
    }
}

static void push_momentary_energy(float energy, uint32_t frames)
{
    if (s_meter.momentary_count == AUDIO_METER_MOMENTARY_BLOCK_CAPACITY) {
        audio_meter_energy_block_t *oldest =
            &s_meter.momentary[s_meter.momentary_head];
        s_meter.momentary_energy_sum -= oldest->energy;
        s_meter.momentary_frame_count -= oldest->frames;
        s_meter.momentary_head =
            (s_meter.momentary_head + 1U) % AUDIO_METER_MOMENTARY_BLOCK_CAPACITY;
        --s_meter.momentary_count;
    }

    uint32_t tail = (s_meter.momentary_head + s_meter.momentary_count) %
                    AUDIO_METER_MOMENTARY_BLOCK_CAPACITY;
    s_meter.momentary[tail] = (audio_meter_energy_block_t){
        .energy = energy,
        .frames = frames,
    };
    ++s_meter.momentary_count;
    s_meter.momentary_energy_sum += energy;
    s_meter.momentary_frame_count += frames;

    while (s_meter.momentary_count > 1U) {
        audio_meter_energy_block_t *oldest =
            &s_meter.momentary[s_meter.momentary_head];
        if (s_meter.momentary_frame_count - oldest->frames < s_meter.momentary_frames) {
            break;
        }
        s_meter.momentary_energy_sum -= oldest->energy;
        s_meter.momentary_frame_count -= oldest->frames;
        s_meter.momentary_head =
            (s_meter.momentary_head + 1U) % AUDIO_METER_MOMENTARY_BLOCK_CAPACITY;
        --s_meter.momentary_count;
    }

    if (s_meter.momentary_frame_count >= s_meter.momentary_frames) {
        float mean = s_meter.momentary_energy_sum /
                     (float)s_meter.momentary_frame_count;
        if (mean > s_meter.result.max_momentary_k_mean_square) {
            s_meter.result.max_momentary_k_mean_square = mean;
        }
    }
}

static uint32_t meter_clock_us(void)
{
#if defined(SYNTH_AUDIO_METER_SINGLE_THREADED_TEST)
    return 0;
#else
    return (uint32_t)esp_timer_get_time();
#endif
}

esp_err_t synth_audio_meter_init(uint32_t sample_rate_hz,
                                 uint32_t audio_block_frames,
                                 uint32_t audio_block_budget_us)
{
    if (audio_block_frames == 0U || audio_block_budget_us == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_meter_biquad_coefficients_t shelf;
    audio_meter_biquad_coefficients_t high_pass;
    if (!calculate_k_weighting_coefficients(sample_rate_hz, &shelf, &high_pass)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t momentary_frames = (uint32_t)(((uint64_t)sample_rate_hz * 400U) / 1000U);
    uint64_t momentary_blocks =
        ((uint64_t)momentary_frames + audio_block_frames - 1U) / audio_block_frames;
    if (momentary_blocks > AUDIO_METER_MOMENTARY_BLOCK_CAPACITY) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_meter = (audio_meter_state_t){
        .sample_rate_hz = sample_rate_hz,
        .audio_block_frames = audio_block_frames,
        .audio_block_budget_us = audio_block_budget_us,
        .momentary_frames = momentary_frames,
        .k_shelf = shelf,
        .k_high_pass = high_pass,
        .initialized = true,
    };
    atomic_init_u32(&s_meter.commands.head, 0);
    atomic_init_u32(&s_meter.commands.tail, 0);
    atomic_init_u32(&s_meter.request_outstanding, 0);
    synth_audio_meter_result_t initial = {.complete = true};
    for (uint32_t slot = 0; slot < AUDIO_METER_SNAPSHOT_SLOT_COUNT; ++slot) {
        atomic_init_u32(&s_meter.snapshots.slot[slot].readers, 0);
        s_meter.snapshots.slot[slot].value = initial;
    }
    atomic_init_u32(&s_meter.snapshots.published, 0);
    return ESP_OK;
}

esp_err_t synth_audio_meter_start(const synth_audio_meter_request_t *request)
{
    if (!s_meter.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request == NULL || request->session_id == 0U ||
        request->tap >= SYNTH_AUDIO_METER_TAP_COUNT || request->target_frames == 0U ||
        (request->on_stop == NULL) != (request->stop_after_frames == 0U) ||
        request->stop_after_frames >= request->target_frames) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t available = 0;
    if (!atomic_compare_exchange_u32(&s_meter.request_outstanding, &available, 1U)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!command_push(request)) {
        atomic_store_release_u32(&s_meter.request_outstanding, 0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void synth_audio_meter_begin_block(void)
{
    if (!s_meter.initialized) {
        return;
    }

    synth_audio_meter_request_t next;
    while (command_pop(&next)) {
        s_meter.pending = next;
        s_meter.has_pending = true;
    }
    if (s_meter.has_pending) {
        reset_accumulators(&s_meter.pending);
        s_meter.has_pending = false;
        if (s_meter.active_request.on_start != NULL) {
            s_meter.active_request.on_start(s_meter.active_request.hook_context);
        }
        snapshot_publish(&s_meter.result);
    }

    if (s_meter.result.active && !s_meter.stop_invoked &&
        s_meter.result.measured_frames >= s_meter.active_request.stop_after_frames) {
        s_meter.stop_invoked = true;
        s_meter.active_request.on_stop(s_meter.active_request.hook_context);
    }
}

bool synth_audio_meter_read(synth_audio_meter_result_t *result)
{
    if (!s_meter.initialized || result == NULL) {
        return false;
    }
    for (;;) {
        uint32_t slot = atomic_load_acquire_u32(&s_meter.snapshots.published);
        atomic_fetch_add_u32(&s_meter.snapshots.slot[slot].readers, 1U);
        if (slot != atomic_load_acquire_u32(&s_meter.snapshots.published)) {
            atomic_fetch_sub_u32(&s_meter.snapshots.slot[slot].readers, 1U);
            continue;
        }
        *result = s_meter.snapshots.slot[slot].value;
        atomic_fetch_sub_u32(&s_meter.snapshots.slot[slot].readers, 1U);
        return true;
    }
}

void synth_audio_meter_process(synth_audio_meter_tap_t tap,
                               const int16_t *interleaved_samples,
                               uint32_t frames,
                               uint8_t channels)
{
    if (!s_meter.initialized || tap >= SYNTH_AUDIO_METER_TAP_COUNT ||
        interleaved_samples == NULL || frames == 0U || channels == 0U ||
        channels > AUDIO_METER_MAX_CHANNELS) {
        return;
    }

    if (!s_meter.result.active || s_meter.result.tap != tap) {
        return;
    }

    uint32_t started_us = meter_clock_us();
    uint64_t remaining = s_meter.result.target_frames - s_meter.result.measured_frames;
    uint32_t measured = remaining < frames ? (uint32_t)remaining : frames;
    float block_k_energy = 0.0f;
    const audio_meter_biquad_coefficients_t shelf_coefficients = s_meter.k_shelf;
    const audio_meter_biquad_coefficients_t high_pass_coefficients =
        s_meter.k_high_pass;

    for (uint32_t frame = 0; frame < measured; ++frame) {
        for (uint8_t channel = 0; channel < channels; ++channel) {
            int16_t sample = interleaved_samples[frame * channels + channel];
            float normalized = (float)sample / 32768.0f;
            float magnitude = normalized < 0.0f ? -normalized : normalized;
            if (magnitude > s_meter.result.sample_peak) {
                s_meter.result.sample_peak = magnitude;
            }
            if (sample == INT16_MIN || sample == INT16_MAX) {
                ++s_meter.result.clipped_samples;
            }
            s_meter.square_sum += normalized * normalized;
            float weighted = k_weight_sample(normalized,
                                             &s_meter.k_state[channel],
                                             &shelf_coefficients,
                                             &high_pass_coefficients);
            block_k_energy += weighted * weighted;
        }
    }

    s_meter.k_energy_sum += block_k_energy;
    s_meter.result.measured_frames += measured;
    push_momentary_energy(block_k_energy, measured);

    uint32_t elapsed_us = meter_clock_us() - started_us;
    if (elapsed_us > s_meter.result.process_time_max_us) {
        s_meter.result.process_time_max_us = elapsed_us;
    }
    if (elapsed_us >= s_meter.audio_block_budget_us) {
        ++s_meter.result.process_deadline_misses;
    }

    if (s_meter.result.measured_frames >= s_meter.result.target_frames) {
        float sample_count = (float)s_meter.result.measured_frames * (float)channels;
        s_meter.result.mean_square = s_meter.square_sum / sample_count;
        s_meter.result.k_weighted_mean_square =
            s_meter.k_energy_sum / (float)s_meter.result.measured_frames;
        s_meter.result.k_weighted_exposure =
            s_meter.k_energy_sum / (float)s_meter.sample_rate_hz;
        s_meter.result.active = false;
        s_meter.result.complete = true;
        if (!s_meter.stop_invoked) {
            s_meter.stop_invoked = true;
            s_meter.active_request.on_stop(s_meter.active_request.hook_context);
        }
        atomic_store_release_u32(&s_meter.request_outstanding, 0);
    }
    snapshot_publish(&s_meter.result);
}
