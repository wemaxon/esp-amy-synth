#include "synth/app/playback.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "synth/amy_engine.h"
#include "synth/app/chord_tools.h"
#include "synth/app/rhythm.h"

static const char *TAG = "SYNTH_APP";

void synth_app_playback_stop_strum(synth_app_state_t *app)
{
    if (app->play.strum_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(app->play.strum_slot));
    }
    synth_app_play_stop_strum(&app->play);
}

void synth_app_playback_start_strum(synth_app_state_t *app, uint8_t slot, const synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_musical_all_off());
    synth_app_play_start_strum(&app->play, slot, chord, xTaskGetTickCount());
    strlcpy(app->status, "STRUM", sizeof(app->status));
}

void synth_app_playback_update_strum(synth_app_state_t *app)
{
    if (app->mode != SYNTH_PLAY_MODE_STRUM || !app->play.strum_active) {
        return;
    }
    if ((app->chords.held_mask & (1U << app->play.strum_slot)) == 0) {
        synth_app_playback_stop_strum(app);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - app->play.strum_next_tick) < 0) {
        return;
    }

    esp_err_t err = synth_amy_chord_voice_on(app->play.strum_slot, &app->play.strum_chord, app->play.strum_voice_index);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NOT_SUPPORTED) {
        app->play.strum_active = false;
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to strum voice %u: %s",
                 app->play.strum_voice_index,
                 esp_err_to_name(err));
        app->play.strum_active = false;
        return;
    }

    ++app->play.strum_voice_index;
    app->play.strum_next_tick = now + pdMS_TO_TICKS(synth_strum_speed_delay_ms(app->play.strum_speed));
}

static uint8_t active_arp_slot(const synth_app_state_t *app)
{
    return synth_app_chord_state_active_valid_slot(&app->chords, app->play.arp_chord_valid);
}

void synth_app_playback_stop_arp(synth_app_state_t *app)
{
    if (app->play.arp_last_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(app->play.arp_last_slot));
    }
    synth_app_play_stop_arp(&app->play);
}

void synth_app_playback_update_arp(synth_app_state_t *app)
{
    if (app->mode != SYNTH_PLAY_MODE_ARPEGGIO) {
        return;
    }

    uint8_t slot = active_arp_slot(app);
    if (slot == SYNTH_APP_PLAY_NO_SLOT) {
        synth_app_playback_stop_arp(app);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - app->play.arp_next_tick) < 0) {
        return;
    }

    const synth_chord_t *source = &app->play.arp_chords[slot];
    if (source->note_count == 0) {
        return;
    }

    if (app->play.arp_last_slot != SYNTH_APP_PLAY_NO_SLOT && app->play.arp_last_slot != slot) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(app->play.arp_last_slot));
    }

    synth_chord_t step = {0};
    synth_app_build_arp_step_chord(app->play.arp_pattern, app->play.arp_step_index, source, &step);

    synth_chord_t output = step;
    if (app->play.arp_chord_mode == SYNTH_ARP_CHORD_MODE_CHORD_ARP) {
        output = *source;
        synth_app_append_step_notes(&output, &step);
    } else if (app->play.arp_chord_mode == SYNTH_ARP_CHORD_MODE_RHYTHM_ARP) {
        output = (app->play.arp_step_index & 1U) == 0 ? *source : step;
        if ((app->play.arp_step_index & 1U) == 0) {
            synth_app_append_step_notes(&output, &step);
        }
    }

    esp_err_t err = synth_amy_chord_on(slot, &output);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play arp step: %s", esp_err_to_name(err));
        return;
    }
    synth_app_chord_state_remember_voice_anchor(&app->chords, &output);

    char note_name[48];
    synth_app_format_chord_notes(&step, note_name, sizeof(note_name));
    ESP_LOGD(TAG,
             "arp %s slot=%u pattern=%s rate=%s mode=%s step=%lu notes=%s",
             source->name,
             (unsigned)(slot + 1),
             synth_arp_pattern_name(app->play.arp_pattern),
             synth_rhythm_rate_name(app->play.arp_rate),
             synth_arp_chord_mode_name(app->play.arp_chord_mode),
             (unsigned long)app->play.arp_step_index,
             note_name);

    app->play.arp_last_slot = slot;
    app->play.arp_next_tick =
        now + pdMS_TO_TICKS(synth_app_rhythm_interval_ms(app->play.arp_rate,
                                                         synth_app_tempo_bpm(&app->tempo),
                                                         app->play.arp_step_index));
    ++app->play.arp_step_index;
}

void synth_app_playback_stop_repeat(synth_app_state_t *app)
{
    if (app->play.repeat_active && app->play.repeat_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(app->play.repeat_slot));
    }
    synth_app_play_stop_repeat(&app->play);
}

void synth_app_playback_update_repeat(synth_app_state_t *app)
{
    if (app->mode != SYNTH_PLAY_MODE_REPEAT) {
        return;
    }

    uint8_t slot = synth_app_chord_state_active_valid_slot(&app->chords, app->chords.held_chord_valid);
    if (slot == SYNTH_APP_PLAY_NO_SLOT) {
        synth_app_playback_stop_repeat(app);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - app->play.repeat_next_tick) < 0) {
        return;
    }

    uint32_t interval_ms = synth_app_rhythm_interval_ms(app->play.repeat_rate, synth_app_tempo_bpm(&app->tempo), 0);
    TickType_t gate_ticks = pdMS_TO_TICKS(interval_ms / 2U);
    if (gate_ticks < pdMS_TO_TICKS(20)) {
        gate_ticks = pdMS_TO_TICKS(20);
    }

    if (app->play.repeat_gate_open) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(app->play.repeat_slot));
        app->play.repeat_gate_open = false;
        app->play.repeat_next_tick = now + gate_ticks;
        return;
    }

    app->play.repeat_slot = slot;
    esp_err_t err = synth_amy_chord_on(slot, &app->chords.held_chords[slot]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to repeat chord: %s", esp_err_to_name(err));
        return;
    }
    synth_app_chord_state_remember_voice_anchor(&app->chords, &app->chords.held_chords[slot]);
    app->play.repeat_active = true;
    app->play.repeat_gate_open = true;
    app->play.repeat_next_tick = now + gate_ticks;
}
