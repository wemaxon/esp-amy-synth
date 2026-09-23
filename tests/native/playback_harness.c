#include "test_check.h"
#include <stddef.h>
#include <string.h>
#include "freertos/task.h"
#include "synth/app/chords.h"
#include "synth/app/play_state.h"
#include "synth/app/rhythm.h"
#include "synth/app/tempo.h"

#include "synth/app/state.h"
#include "synth/amy_engine.h"
#include "synth/play_mode.h"
#include "synth/app/playback.h"

static TickType_t clock_tick;
static unsigned on_count, off_count, voice_count;
static uint8_t last_slot;
static synth_chord_t last_chord;
static esp_err_t engine_result;

TickType_t xTaskGetTickCount(void) { return clock_tick; }
esp_err_t synth_amy_chord_off(uint8_t slot) { ++off_count; last_slot = slot; return ESP_OK; }
esp_err_t synth_amy_musical_all_off(void) { ++off_count; return ESP_OK; }
esp_err_t synth_amy_chord_on(uint8_t slot, const synth_chord_t *chord)
{
    ++on_count; last_slot = slot; last_chord.notes[0] = chord->notes[0]; return engine_result;
}
esp_err_t synth_amy_chord_voice_on(uint8_t slot, const synth_chord_t *chord, uint8_t voice)
{
    (void)chord; (void)voice; ++voice_count; last_slot = slot; return engine_result;
}
/* Only log formatting is replaced; timing and note construction are real C. */
void synth_app_format_chord_notes(const synth_chord_t *chord, char *out, size_t size)
{
    (void)chord; if (size) out[0] = 0;
}
static synth_app_state_t initial(synth_play_mode_t mode)
{
    on_count = off_count = voice_count = 0;
    engine_result = ESP_OK;
    clock_tick = 100;
    synth_app_state_t app = {
        .mode = mode, .play = SYNTH_APP_PLAY_STATE_INITIALIZER,
        .tempo = SYNTH_APP_TEMPO_INITIALIZER,
        .chords = {.held_mask = 1, .held_chord_valid = {true},
                   .held_chords = {{.note_count = 3, .notes = {60, 64, 67}}}},
    };
    return app;
}
int32_t host_vector_playback_strum_release(void)
{
    synth_app_state_t app = initial(SYNTH_PLAY_MODE_STRUM);
    synth_app_playback_start_strum(&app, 0, &app.chords.held_chords[0]);
    synth_app_playback_update_strum(&app);
    if (voice_count != 1 || off_count != 1 || app.play.strum_voice_index != 1) return 0;
    synth_app_playback_update_strum(&app);
    if (voice_count != 1) return 0;
    app.chords.held_mask = 0;
    synth_app_playback_update_strum(&app);
    return off_count == 2 && !app.play.strum_active;
}
int32_t host_vector_playback_strum_error(void)
{
    synth_app_state_t app = initial(SYNTH_PLAY_MODE_STRUM);
    synth_app_playback_start_strum(&app, 0, &app.chords.held_chords[0]);
    engine_result = ESP_ERR_NOT_FOUND;
    synth_app_playback_update_strum(&app);
    return voice_count == 1 && !app.play.strum_active && app.play.strum_voice_index == 0;
}
int32_t host_vector_playback_repeat_wrap(void)
{
    synth_app_state_t app = initial(SYNTH_PLAY_MODE_REPEAT);
    clock_tick = UINT32_MAX - 49;
    app.play.repeat_next_tick = clock_tick;
    synth_app_playback_update_repeat(&app);
    if (on_count != 1 || app.play.repeat_next_tick != 75) return 0;
    clock_tick = 74;
    synth_app_playback_update_repeat(&app);
    if (off_count) return 0;
    clock_tick = 75;
    synth_app_playback_update_repeat(&app);
    return off_count == 1 && !app.play.repeat_gate_open && app.play.repeat_next_tick == 200;
}
int32_t host_vector_playback_arp_delayed_poll(void)
{
    synth_app_state_t app = initial(SYNTH_PLAY_MODE_ARPEGGIO);
    app.play.arp_chord_valid[0] = true;
    app.play.arp_chords[0].note_count = 1;
    app.play.arp_chords[0].notes[0] = 60;
    app.play.arp_chords[0].notes[1] = 64;
    app.play.arp_chords[0].notes[2] = 67;
    app.play.arp_next_tick = 100;
    synth_app_playback_update_arp(&app);
    if (on_count != 1 || last_chord.notes[0] != 60 || app.play.arp_step_index != 1) return 0;
    clock_tick = 1000;
    synth_app_playback_update_arp(&app);
    return on_count == 2 && last_chord.notes[0] == 60 &&
           app.play.arp_step_index == 2 && app.play.arp_next_tick == 1250;
}

int main(void)
{
    CHECK_VECTOR(host_vector_playback_strum_release, 1);
    CHECK_VECTOR(host_vector_playback_strum_error, 1);
    CHECK_VECTOR(host_vector_playback_repeat_wrap, 1);
    CHECK_VECTOR(host_vector_playback_arp_delayed_poll, 1);
    return 0;
}
