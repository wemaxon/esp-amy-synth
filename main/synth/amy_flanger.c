#include "synth/amy_flanger.h"

#include <math.h>
#include <string.h>

#include "amy.h"

#define FLANGER_BUFFER_LEN 512U
#define FLANGER_BUFFER_MASK (FLANGER_BUFFER_LEN - 1U)

static const float FLANGER_RATE_HZ = 0.35f;
static const float FLANGER_MIN_DELAY_MS = 0.70f;
static const float FLANGER_MAX_DELAY_MS = 6.00f;
static const float FLANGER_FEEDBACK = 0.45f;
static const float FLANGER_DRY = 0.75f;
static const float FLANGER_WET = 0.32f;
static const float TWO_PI = 6.283185307f;

static bool s_enabled;
static float s_buffer[AMY_NCHANS][FLANGER_BUFFER_LEN];
static uint16_t s_write_index;
static float s_phase;

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int16_t clamp_int16(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)value;
}

static float delay_sample(uint8_t channel, float delay_samples)
{
    uint16_t base_delay = (uint16_t)delay_samples;
    float frac = delay_samples - (float)base_delay;
    uint16_t index_a = (uint16_t)((s_write_index + FLANGER_BUFFER_LEN - base_delay) &
                                  FLANGER_BUFFER_MASK);
    uint16_t index_b = (uint16_t)((index_a + FLANGER_BUFFER_LEN - 1U) &
                                  FLANGER_BUFFER_MASK);
    float a = s_buffer[channel][index_a];
    float b = s_buffer[channel][index_b];
    return a + (b - a) * frac;
}

void synth_amy_flanger_set_enabled(bool enabled)
{
    if (enabled != s_enabled) {
        memset(s_buffer, 0, sizeof(s_buffer));
        s_write_index = 0;
        s_phase = 0.0f;
    }
    s_enabled = enabled;
}

void synth_amy_flanger_process(int16_t *block)
{
    if (!s_enabled || block == NULL) {
        return;
    }

    float min_delay = (FLANGER_MIN_DELAY_MS * (float)AMY_SAMPLE_RATE) / 1000.0f;
    float max_delay = (FLANGER_MAX_DELAY_MS * (float)AMY_SAMPLE_RATE) / 1000.0f;
    float delay_range = max_delay - min_delay;
    float phase_increment = FLANGER_RATE_HZ / (float)AMY_SAMPLE_RATE;

    for (uint16_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        for (uint8_t channel = 0; channel < AMY_NCHANS; ++channel) {
            float channel_phase = s_phase;
            if (AMY_NCHANS > 1 && channel == 1) {
                channel_phase += 0.25f;
                if (channel_phase >= 1.0f) {
                    channel_phase -= 1.0f;
                }
            }

            float lfo = 0.5f + 0.5f * sinf(TWO_PI * channel_phase);
            float delayed = delay_sample(channel, min_delay + delay_range * lfo);
            uint16_t sample_index = (uint16_t)(i * AMY_NCHANS + channel);
            float input = (float)block[sample_index] / 32768.0f;
            float feedback_sample = clamp_float(input + delayed * FLANGER_FEEDBACK, -1.0f, 1.0f);
            float output = input * FLANGER_DRY + delayed * FLANGER_WET;

            s_buffer[channel][s_write_index] = feedback_sample;
            block[sample_index] = clamp_int16(output * 32767.0f);
        }

        s_phase += phase_increment;
        if (s_phase >= 1.0f) {
            s_phase -= 1.0f;
        }
        s_write_index = (uint16_t)((s_write_index + 1U) & FLANGER_BUFFER_MASK);
    }
}
