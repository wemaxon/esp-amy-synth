#include "synth/app/drum_modes.h"

#include <stdint.h>

#include "esp_log.h"
#include "synth/app/rhythm.h"

#define DRUM_BIT(voice) ((uint8_t)(1U << (uint8_t)(voice)))

static const char *TAG = "SYNTH_DRUMS";

static bool direction_rate(control_joystick_direction_t direction, synth_rhythm_rate_t *rate)
{
    if (rate == NULL) {
        return false;
    }

    switch (direction) {
    case CONTROL_JOYSTICK_UP:
        *rate = SYNTH_RHYTHM_RATE_QUARTER;
        return true;
    case CONTROL_JOYSTICK_RIGHT:
        *rate = SYNTH_RHYTHM_RATE_EIGHTH;
        return true;
    case CONTROL_JOYSTICK_DOWN:
        *rate = SYNTH_RHYTHM_RATE_SIXTEENTH;
        return true;
    case CONTROL_JOYSTICK_LEFT:
        *rate = SYNTH_RHYTHM_RATE_THIRTY_SECOND;
        return true;
    case CONTROL_JOYSTICK_UP_RIGHT:
    case CONTROL_JOYSTICK_UP_LEFT:
        *rate = SYNTH_RHYTHM_RATE_SWING_EIGHTH;
        return true;
    case CONTROL_JOYSTICK_DOWN_RIGHT:
        *rate = SYNTH_RHYTHM_RATE_SWING_SIXTEENTH;
        return true;
    case CONTROL_JOYSTICK_DOWN_LEFT:
        *rate = SYNTH_RHYTHM_RATE_SIXTEENTH_TRIPLET;
        return true;
    case CONTROL_JOYSTICK_CENTER:
    default:
        return false;
    }
}

static void trigger_voice(synth_app_drum_modes_t *drums, synth_drum_voice_t voice, bool accented)
{
    if (drums == NULL || voice >= SYNTH_DRUM_VOICE_COUNT) {
        return;
    }
    uint8_t velocity = synth_drums_default_velocity(voice, accented);
    esp_err_t err = synth_drums_hit(drums->loop.kit, voice, velocity);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drum hit failed voice=%u err=%s", (unsigned)voice, esp_err_to_name(err));
    }
}

bool synth_app_drums_mode_uses_buttons(synth_play_mode_t mode)
{
    return mode == SYNTH_PLAY_MODE_DRUM_MODE ||
           mode == SYNTH_PLAY_MODE_DRUM_LOOPS ||
           mode == SYNTH_PLAY_MODE_AUTO_DRUM;
}

synth_app_drum_result_t synth_app_drums_set_kit_for_button(synth_app_drum_modes_t *drums,
                                                           uint8_t button)
{
    if (drums == NULL) {
        return (synth_app_drum_result_t){0};
    }
    drums->loop.kit = synth_drum_kit_for_button(button);
    ESP_LOGI(TAG, "kit=%s", synth_drum_kit_name(drums->loop.kit));
    return (synth_app_drum_result_t){
        .handled = true,
        .ui_dirty = true,
        .status = "KIT",
        .label = synth_drum_kit_name(drums->loop.kit),
    };
}

synth_app_drum_result_t synth_app_drums_button_press(synth_app_drum_modes_t *drums,
                                                     synth_play_mode_t mode,
                                                     uint8_t button,
                                                     control_joystick_direction_t direction,
                                                     uint16_t bpm,
                                                     TickType_t now)
{
    if (drums == NULL || !synth_app_drums_mode_uses_buttons(mode)) {
        return (synth_app_drum_result_t){0};
    }

    if (mode == SYNTH_PLAY_MODE_DRUM_LOOPS) {
        synth_drum_loop_style_t style = synth_drum_loop_style_for_button(button);
        if (drums->loop.running && drums->loop.style == style) {
            synth_app_drums_stop_loop(drums);
            return (synth_app_drum_result_t){
                .handled = true,
                .ui_dirty = true,
                .status = "BEAT OFF",
                .label = synth_drum_loop_style_name(style),
            };
        }
        synth_app_drum_transport_start(&drums->loop, style, bpm, now);
        ESP_LOGI(TAG,
                 "drum loop style=%s variation=%s kit=%s",
                 synth_drum_loop_style_name(drums->loop.style),
                 synth_drum_loop_variation_name(drums->loop.variation),
                 synth_drum_kit_name(drums->loop.kit));
        return (synth_app_drum_result_t){
            .handled = true,
            .ui_dirty = true,
            .status = "BEAT ON",
            .label = synth_drum_loop_style_name(style),
        };
    }

    synth_drum_voice_t voice = synth_drum_voice_for_button(button);
    trigger_voice(drums, voice, true);

    synth_rhythm_rate_t rate = SYNTH_RHYTHM_RATE_EIGHTH;
    if (direction_rate(direction, &rate)) {
        drums->auto_mask |= DRUM_BIT(voice);
        drums->auto_rate[voice] = rate;
        drums->auto_step_index[voice] = 0;
        drums->auto_next_tick[voice] = synth_app_rhythm_advance_deadline(
            now,
            rate,
            bpm,
            &drums->auto_step_index[voice],
            now);
    }

    return (synth_app_drum_result_t){
        .handled = true,
        .ui_dirty = true,
        .status = synth_drum_voice_name(voice),
        .label = synth_drum_voice_name(voice),
    };
}

synth_app_drum_result_t synth_app_drums_button_release(synth_app_drum_modes_t *drums,
                                                       synth_play_mode_t mode,
                                                       uint8_t button)
{
    if (drums == NULL || mode != SYNTH_PLAY_MODE_AUTO_DRUM) {
        return (synth_app_drum_result_t){0};
    }

    synth_drum_voice_t voice = synth_drum_voice_for_button(button);
    drums->auto_mask &= (uint8_t)~DRUM_BIT(voice);
    return (synth_app_drum_result_t){
        .handled = true,
        .ui_dirty = true,
        .status = drums->auto_mask == 0 ? "READY" : "AUTO DRUM",
        .label = drums->auto_mask == 0 ? "---" : synth_drum_voice_name(voice),
    };
}

synth_app_drum_result_t synth_app_drums_joystick(synth_app_drum_modes_t *drums,
                                                 synth_play_mode_t mode,
                                                 uint8_t held_mask,
                                                 control_joystick_direction_t direction,
                                                 TickType_t now)
{
    if (drums == NULL || !synth_app_drums_mode_uses_buttons(mode)) {
        return (synth_app_drum_result_t){0};
    }

    if (mode == SYNTH_PLAY_MODE_DRUM_LOOPS) {
        switch (direction) {
        case CONTROL_JOYSTICK_LEFT:
            drums->loop.kit = synth_drum_kit_step(drums->loop.kit, -1);
            return (synth_app_drum_result_t){
                .handled = true,
                .ui_dirty = true,
                .status = "KIT",
                .label = synth_drum_kit_name(drums->loop.kit),
            };
        case CONTROL_JOYSTICK_RIGHT:
            drums->loop.kit = synth_drum_kit_step(drums->loop.kit, 1);
            return (synth_app_drum_result_t){
                .handled = true,
                .ui_dirty = true,
                .status = "KIT",
                .label = synth_drum_kit_name(drums->loop.kit),
            };
        case CONTROL_JOYSTICK_UP:
            if (held_mask != 0) {
                drums->loop.variation = synth_drum_loop_variation_step(drums->loop.variation, 1);
                return (synth_app_drum_result_t){
                    .handled = true,
                    .ui_dirty = true,
                    .status = "VAR",
                    .label = synth_drum_loop_variation_short_name(drums->loop.variation),
                };
            }
            break;
        case CONTROL_JOYSTICK_DOWN:
            if (held_mask != 0) {
                drums->loop.variation = synth_drum_loop_variation_step(drums->loop.variation, -1);
                return (synth_app_drum_result_t){
                    .handled = true,
                    .ui_dirty = true,
                    .status = "VAR",
                    .label = synth_drum_loop_variation_short_name(drums->loop.variation),
                };
            }
            break;
        default:
            break;
        }
        return (synth_app_drum_result_t){.handled = true};
    }

    synth_rhythm_rate_t rate = SYNTH_RHYTHM_RATE_EIGHTH;
    bool has_rate = direction_rate(direction, &rate);
    for (uint8_t voice = 0; voice < SYNTH_DRUM_VOICE_COUNT; ++voice) {
        uint8_t bit = DRUM_BIT(voice);
        if ((held_mask & bit) == 0) {
            continue;
        }
        if (!has_rate) {
            drums->auto_mask &= (uint8_t)~bit;
            continue;
        }
        if ((drums->auto_mask & bit) == 0 || drums->auto_rate[voice] != rate) {
            drums->auto_mask |= bit;
            drums->auto_rate[voice] = rate;
            drums->auto_step_index[voice] = 0;
            drums->auto_next_tick[voice] = now;
        }
    }

    return (synth_app_drum_result_t){
        .handled = true,
        .ui_dirty = held_mask != 0,
        .status = has_rate && held_mask != 0 ? "AUTO DRUM" : "DRUM",
        .label = has_rate && held_mask != 0 ?
            synth_rhythm_rate_name(rate) : synth_drum_kit_name(drums->loop.kit),
    };
}

bool synth_app_drums_update(synth_app_drum_modes_t *drums, uint16_t bpm, TickType_t now)
{
    if (drums == NULL) {
        return false;
    }

    bool changed = synth_app_drum_transport_update(&drums->loop, bpm, now);

    for (uint8_t voice = 0; voice < SYNTH_DRUM_VOICE_COUNT; ++voice) {
        uint8_t bit = DRUM_BIT(voice);
        if ((drums->auto_mask & bit) == 0 ||
            (int32_t)(now - drums->auto_next_tick[voice]) < 0) {
            continue;
        }
        trigger_voice(drums, (synth_drum_voice_t)voice, true);
        drums->auto_next_tick[voice] = synth_app_rhythm_advance_deadline(
            drums->auto_next_tick[voice],
            drums->auto_rate[voice],
            bpm,
            &drums->auto_step_index[voice],
            now);
        changed = true;
    }
    return changed;
}

void synth_app_drums_stop_auto(synth_app_drum_modes_t *drums)
{
    if (drums != NULL) {
        drums->auto_mask = 0;
    }
}

void synth_app_drums_stop_loop(synth_app_drum_modes_t *drums)
{
    if (drums != NULL) {
        synth_app_drum_transport_stop(&drums->loop);
    }
}

bool synth_app_drums_loop_active(const synth_app_drum_modes_t *drums)
{
    return drums != NULL && drums->loop.running;
}

const char *synth_app_drums_auto_rate_name(const synth_app_drum_modes_t *drums)
{
    if (drums == NULL || drums->auto_mask == 0) {
        return "-";
    }
    for (uint8_t voice = 0; voice < SYNTH_DRUM_VOICE_COUNT; ++voice) {
        if ((drums->auto_mask & DRUM_BIT(voice)) != 0) {
            return synth_rhythm_rate_name(drums->auto_rate[voice]);
        }
    }
    return "-";
}
