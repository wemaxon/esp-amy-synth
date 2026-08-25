#include "synth/app/drum_transport.h"

#include "esp_err.h"
#include "esp_log.h"
#include "synth/app/rhythm.h"

#define DRUM_BIT(voice) ((uint8_t)(1U << (uint8_t)(voice)))

static const char *TAG = "DRUM_TRANSPORT";

static void trigger_voice(const synth_app_drum_transport_t *transport,
                          synth_drum_voice_t voice,
                          bool accented)
{
    uint8_t velocity = synth_drums_default_velocity(voice, accented);
    esp_err_t err = synth_drums_hit(transport->kit, voice, velocity);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drum hit failed voice=%u err=%s", (unsigned)voice, esp_err_to_name(err));
    }
}

static void trigger_step(const synth_app_drum_transport_t *transport, uint8_t step)
{
    synth_drum_pattern_step_t pattern =
        synth_drum_pattern_step(transport->style, transport->variation, step);

    for (uint8_t voice = 0; voice < SYNTH_DRUM_VOICE_COUNT; ++voice) {
        uint8_t bit = DRUM_BIT(voice);
        if ((pattern.mask & bit) != 0) {
            trigger_voice(transport,
                          (synth_drum_voice_t)voice,
                          (pattern.accent_mask & bit) != 0);
        }
    }
}

void synth_app_drum_transport_start(synth_app_drum_transport_t *transport,
                                    synth_drum_loop_style_t style,
                                    TickType_t now)
{
    if (transport == NULL) {
        return;
    }

    transport->style = style;
    transport->running = true;
    transport->step_index = 0;
    transport->next_tick = now;
}

void synth_app_drum_transport_stop(synth_app_drum_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    transport->running = false;
    transport->step_index = 0;
}

bool synth_app_drum_transport_update(synth_app_drum_transport_t *transport,
                                     uint16_t bpm,
                                     TickType_t now)
{
    if (transport == NULL || !transport->running ||
        (int32_t)(now - transport->next_tick) < 0) {
        return false;
    }

    trigger_step(transport,
                 (uint8_t)(transport->step_index % SYNTH_DRUM_PATTERN_STEPS));
    transport->next_tick = synth_app_rhythm_advance_deadline(
        transport->next_tick,
        SYNTH_RHYTHM_RATE_SIXTEENTH,
        bpm,
        &transport->step_index,
        now);
    return true;
}
