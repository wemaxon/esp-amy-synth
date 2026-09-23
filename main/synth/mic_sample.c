#include "synth/mic_sample.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"

#define MIC_FADE_MS 5U

static int16_t *s_samples;
static uint32_t s_sample_rate_hz;
static uint32_t s_capacity_frames;
static atomic_uint s_state;
static atomic_uint s_frames;
static atomic_int s_peak;
static atomic_uint s_clipped_samples;
static atomic_int s_digital_gain_q16;
static atomic_bool s_stop_requested;
static bool s_has_installed_sample;
static synth_pitch_result_t s_pitch;

esp_err_t synth_mic_sample_init(uint32_t sample_rate_hz, uint32_t max_seconds)
{
    if (sample_rate_hz == 0 || max_seconds == 0 || max_seconds > UINT32_MAX / sample_rate_hz) {
        return ESP_ERR_INVALID_ARG;
    }
    s_sample_rate_hz = sample_rate_hz;
    s_capacity_frames = sample_rate_hz * max_seconds;
    atomic_store(&s_state, SYNTH_MIC_SAMPLE_EMPTY);
    return ESP_OK;
}

esp_err_t synth_mic_sample_begin(float digital_gain)
{
    if (s_sample_rate_hz == 0 || s_capacity_frames == 0 || !isfinite(digital_gain) || digital_gain <= 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_load_explicit(&s_state, memory_order_acquire) == SYNTH_MIC_SAMPLE_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_samples == NULL) {
        s_samples = heap_caps_malloc((size_t)s_capacity_frames * sizeof(*s_samples),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_samples == NULL) {
            atomic_store(&s_state, SYNTH_MIC_SAMPLE_ERROR);
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_pitch, 0, sizeof(s_pitch));
    atomic_store(&s_frames, 0);
    atomic_store(&s_peak, 0);
    atomic_store(&s_clipped_samples, 0);
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_digital_gain_q16, (int)lrintf(digital_gain * 65536.0f));
    atomic_store_explicit(&s_state, SYNTH_MIC_SAMPLE_RECORDING, memory_order_release);
    return ESP_OK;
}

void synth_mic_sample_request_stop(void)
{
    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
}

void synth_mic_sample_cancel(void)
{
    atomic_store_explicit(&s_stop_requested, false, memory_order_relaxed);
    atomic_store_explicit(&s_state,
                          s_has_installed_sample ? SYNTH_MIC_SAMPLE_READY : SYNTH_MIC_SAMPLE_EMPTY,
                          memory_order_release);
}

bool synth_mic_sample_is_recording(void)
{
    return atomic_load_explicit(&s_state, memory_order_acquire) == SYNTH_MIC_SAMPLE_RECORDING;
}

void synth_mic_sample_process_input(const int16_t *interleaved,
                                    uint32_t frames,
                                    uint8_t channels,
                                    uint8_t input_channel)
{
    if (atomic_load_explicit(&s_state, memory_order_acquire) != SYNTH_MIC_SAMPLE_RECORDING ||
        interleaved == NULL || channels == 0 || input_channel >= channels) {
        return;
    }
    if (atomic_load_explicit(&s_stop_requested, memory_order_acquire)) {
        atomic_store_explicit(&s_state, SYNTH_MIC_SAMPLE_CAPTURED, memory_order_release);
        return;
    }

    uint32_t write_frame = atomic_load_explicit(&s_frames, memory_order_relaxed);
    int32_t gain_q16 = atomic_load_explicit(&s_digital_gain_q16, memory_order_relaxed);
    int32_t peak = atomic_load_explicit(&s_peak, memory_order_relaxed);
    uint32_t clipped = atomic_load_explicit(&s_clipped_samples, memory_order_relaxed);
    synth_mic_capture_meter_t meter = {
        .peak = (uint16_t)peak,
        .clipped_samples = clipped,
    };
    uint32_t copied = synth_mic_copy_input_channel(s_samples,
                                                    s_capacity_frames,
                                                    write_frame,
                                                    interleaved,
                                                    frames,
                                                    channels,
                                                    input_channel,
                                                    gain_q16,
                                                    &meter);
    write_frame += copied;
    atomic_store_explicit(&s_peak, meter.peak, memory_order_relaxed);
    atomic_store_explicit(&s_clipped_samples, meter.clipped_samples, memory_order_relaxed);
    atomic_store_explicit(&s_frames, write_frame, memory_order_release);
    if (write_frame == s_capacity_frames) {
        atomic_store_explicit(&s_state, SYNTH_MIC_SAMPLE_CAPTURED, memory_order_release);
    }
}

static void apply_edge_fades(uint32_t frames)
{
    uint32_t fade_frames = (s_sample_rate_hz * MIC_FADE_MS) / 1000U;
    if (fade_frames * 2U > frames) {
        fade_frames = frames / 2U;
    }
    for (uint32_t i = 0; i < fade_frames; ++i) {
        s_samples[i] = (int16_t)(((int32_t)s_samples[i] * (int32_t)i) / (int32_t)fade_frames);
        uint32_t tail = frames - 1U - i;
        s_samples[tail] = (int16_t)(((int32_t)s_samples[tail] * (int32_t)i) / (int32_t)fade_frames);
    }
}

esp_err_t synth_mic_sample_analyze(void)
{
    unsigned expected = SYNTH_MIC_SAMPLE_CAPTURED;
    if (!atomic_compare_exchange_strong(&s_state, &expected, SYNTH_MIC_SAMPLE_ANALYZING)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t frames = atomic_load_explicit(&s_frames, memory_order_acquire);
    if (frames == 0) {
        atomic_store(&s_state, SYNTH_MIC_SAMPLE_ERROR);
        return ESP_ERR_INVALID_SIZE;
    }

    synth_pitch_detect(s_samples, frames, s_sample_rate_hz, &s_pitch);
    apply_edge_fades(frames);
    return ESP_OK;
}

void synth_mic_sample_mark_installed(bool installed)
{
    if (installed) {
        s_has_installed_sample = true;
    }
    atomic_store_explicit(&s_state,
                          installed ? SYNTH_MIC_SAMPLE_READY : SYNTH_MIC_SAMPLE_ERROR,
                          memory_order_release);
}

void synth_mic_sample_snapshot(synth_mic_sample_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->state = (synth_mic_sample_state_t)atomic_load_explicit(&s_state, memory_order_acquire);
    snapshot->frames = atomic_load_explicit(&s_frames, memory_order_acquire);
    snapshot->capacity_frames = s_capacity_frames;
    snapshot->peak = (uint16_t)atomic_load_explicit(&s_peak, memory_order_relaxed);
    snapshot->clipped_samples = atomic_load_explicit(&s_clipped_samples, memory_order_relaxed);
    snapshot->pitch = s_pitch;
}

const int16_t *synth_mic_sample_data(uint32_t *frames)
{
    if (frames != NULL) {
        *frames = atomic_load_explicit(&s_frames, memory_order_acquire);
    }
    return s_samples;
}
