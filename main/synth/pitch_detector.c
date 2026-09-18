#include "synth/pitch_detector.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define PITCH_WINDOW_FRAMES 4096U
#define PITCH_MIN_HZ 55.0f
#define PITCH_MAX_HZ 1760.0f
#define PITCH_YIN_THRESHOLD 0.15f
#define PITCH_MIN_CONFIDENCE 0.80f
#define PITCH_MIN_RMS 0.003f
#define PITCH_MAX_LAG 802U

static float s_difference[PITCH_MAX_LAG + 1U];

static uint32_t strongest_window_start(const int16_t *samples, uint32_t frame_count)
{
    if (frame_count <= PITCH_WINDOW_FRAMES) {
        return 0;
    }

    uint64_t best_energy = 0;
    uint32_t best_start = 0;
    const uint32_t step = PITCH_WINDOW_FRAMES / 2U;
    for (uint32_t start = 0; start + PITCH_WINDOW_FRAMES <= frame_count; start += step) {
        uint64_t energy = 0;
        for (uint32_t i = 0; i < PITCH_WINDOW_FRAMES; ++i) {
            int32_t sample = samples[start + i];
            energy += (uint64_t)(sample * sample);
        }
        if (energy > best_energy) {
            best_energy = energy;
            best_start = start;
        }
    }
    return best_start;
}

void synth_pitch_detect(const int16_t *samples,
                        uint32_t frame_count,
                        uint32_t sample_rate_hz,
                        synth_pitch_result_t *result)
{
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    if (samples == NULL || sample_rate_hz == 0) {
        return;
    }

    uint32_t window_frames = frame_count < PITCH_WINDOW_FRAMES ? frame_count : PITCH_WINDOW_FRAMES;
    uint32_t min_lag = (uint32_t)((float)sample_rate_hz / PITCH_MAX_HZ);
    uint32_t max_lag = (uint32_t)((float)sample_rate_hz / PITCH_MIN_HZ);
    if (min_lag < 2U) {
        min_lag = 2U;
    }
    if (max_lag > PITCH_MAX_LAG) {
        max_lag = PITCH_MAX_LAG;
    }
    if (window_frames < max_lag * 2U) {
        return;
    }

    const int16_t *window = &samples[strongest_window_start(samples, frame_count)];
    double energy = 0.0;
    for (uint32_t i = 0; i < window_frames; ++i) {
        float sample = (float)window[i] / 32768.0f;
        energy += (double)sample * sample;
    }
    float rms = sqrtf((float)(energy / window_frames));
    if (rms < PITCH_MIN_RMS) {
        return;
    }

    memset(s_difference, 0, (max_lag + 1U) * sizeof(s_difference[0]));
    for (uint32_t lag = 1; lag <= max_lag; ++lag) {
        double difference = 0.0;
        uint32_t comparisons = window_frames - lag;
        for (uint32_t i = 0; i < comparisons; ++i) {
            float delta = (float)window[i] - (float)window[i + lag];
            difference += (double)delta * delta;
        }
        s_difference[lag] = (float)difference;
    }

    float running_sum = 0.0f;
    s_difference[0] = 1.0f;
    for (uint32_t lag = 1; lag <= max_lag; ++lag) {
        running_sum += s_difference[lag];
        s_difference[lag] = running_sum > 0.0f
                                ? s_difference[lag] * (float)lag / running_sum
                                : 1.0f;
    }

    uint32_t selected = 0;
    for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
        if (s_difference[lag] < PITCH_YIN_THRESHOLD) {
            while (lag < max_lag && s_difference[lag + 1U] < s_difference[lag]) {
                ++lag;
            }
            selected = lag;
            break;
        }
    }
    if (selected == 0) {
        float best = 1.0f;
        for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
            if (s_difference[lag] < best) {
                best = s_difference[lag];
                selected = lag;
            }
        }
    }
    if (selected == 0) {
        return;
    }

    float refined_lag = (float)selected;
    if (selected > min_lag && selected < max_lag) {
        float left = s_difference[selected - 1U];
        float center = s_difference[selected];
        float right = s_difference[selected + 1U];
        float denominator = left - 2.0f * center + right;
        if (fabsf(denominator) > 1.0e-9f) {
            refined_lag += 0.5f * (left - right) / denominator;
        }
    }

    float confidence = 1.0f - s_difference[selected];
    float frequency = (float)sample_rate_hz / refined_lag;
    if (confidence < PITCH_MIN_CONFIDENCE || frequency < PITCH_MIN_HZ || frequency > PITCH_MAX_HZ) {
        return;
    }

    result->pitched = true;
    result->frequency_hz = frequency;
    result->midi_note = 69.0f + 12.0f * log2f(frequency / 440.0f);
    result->confidence = confidence;
}
