#include "diagnostics/amy_patch_browser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"
#include "synth/amy_engine.h"
#include "synth/app/wheel.h"

static const char *TAG = "DIAG_AMY_PATCH";

#define PATCH_BROWSER_KEY_COUNT 7U

static const TickType_t UI_REFRESH_TICKS = pdMS_TO_TICKS(100);
static const TickType_t UI_PAGE_TIMEOUT_TICKS = pdMS_TO_TICKS(25);
static const uint8_t PATCH_BROWSER_BANK_STEP = 8;
static const float PATCH_BROWSER_VELOCITY = 0.55f;
static const uint8_t TEST_NOTES[PATCH_BROWSER_KEY_COUNT] = {
    60, 62, 64, 65, 67, 69, 71,
};
static const control_switch_t CHORD_KEYS[PATCH_BROWSER_KEY_COUNT] = {
    CONTROL_CHORD_1,
    CONTROL_CHORD_2,
    CONTROL_CHORD_3,
    CONTROL_CHORD_4,
    CONTROL_CHORD_5,
    CONTROL_CHORD_6,
    CONTROL_CHORD_7,
};

static const char *const PATCH_NAMES[SYNTH_AMY_BUILTIN_PATCH_COUNT] = {
    "Juno A11 Brass Set 1",
    "Juno A12 Brass Swell",
    "Juno A13 Trumpet",
    "Juno A14 Flutes",
    "Juno A15 Moving Strings",
    "Juno A16 Brass & Strings",
    "Juno A17 Choir",
    "Juno A18 Piano I",
    "Juno A21 Organ I",
    "Juno A22 Organ II",
    "Juno A23 Combo Organ",
    "Juno A24 Calliope",
    "Juno A25 Donald Pluck",
    "Juno A26 Celeste* (1 oct.up)",
    "Juno A27 Elect. Piano I",
    "Juno A28 Elect. Piano II",
    "Juno A31 Clock Chimes* (1 oct. up)",
    "Juno A32 Steel Drums",
    "Juno A33 Xylophone",
    "Juno A34 Brass III",
    "Juno A35 Fanfare",
    "Juno A36 String III",
    "Juno A37 Pizzicato",
    "Juno A38 High Strings",
    "Juno A41 Bass clarinet",
    "Juno A42 English Horn",
    "Juno A43 Brass Ensemble",
    "Juno A44 Guitar",
    "Juno A45 Koto",
    "Juno A46 Dark Pluck",
    "Juno A47 Funky I",
    "Juno A48 Synth Bass I (unison)",
    "Juno A51 Lead I",
    "Juno A52 Lead II",
    "Juno A53 Lead III",
    "Juno A54 Funky II",
    "Juno A55 Synth Bass II",
    "Juno A56 Funky III",
    "Juno A57 Thud Wah",
    "Juno A58 Going Up",
    "Juno A61 Piano II",
    "Juno A62 Clav",
    "Juno A63 Frontier Organ",
    "Juno A64 Snare Drum (unison)",
    "Juno A65 Tom Toms (unison)",
    "Juno A66 Timpani (unison)",
    "Juno A67 Shaker",
    "Juno A68 Synth Pad",
    "Juno A71 Sweep I",
    "Juno A72 Pluck Sweep",
    "Juno A73 Repeater",
    "Juno A74 Sweep II",
    "Juno A75 Pluck Bell",
    "Juno A76 Dark Synth Piano",
    "Juno A77 Sustainer",
    "Juno A78 Wah Release",
    "Juno A81 Gong (play low chords)",
    "Juno A82 Resonance Funk",
    "Juno A83 Drum Booms* (1 oct. down)",
    "Juno A84 Dust Storm",
    "Juno A85 Rocket Men",
    "Juno A86 Hand Claps",
    "Juno A87 FX Sweep",
    "Juno A88 Caverns",
    "Juno B11 Strings",
    "Juno B12 Violin",
    "Juno B13 Chorus Vibes",
    "Juno B14 Organ 1",
    "Juno B15 Harpsichord 1",
    "Juno B16 Recorder",
    "Juno B17 Perc. Pluck",
    "Juno B18 Noise Sweep",
    "Juno B21 Space Chimes",
    "Juno B22 Nylon Guitar",
    "Juno B23 Orchestral Pad",
    "Juno B24 Bright Pluck",
    "Juno B25 Organ Bell",
    "Juno B26 Accordion",
    "Juno B27 FX Rise 1",
    "Juno B28 FX Rise 2",
    "Juno B31 Brass",
    "Juno B32 Helicopter",
    "Juno B33 Lute",
    "Juno B34 Chorus Funk",
    "Juno B35 Tomita",
    "Juno B36 FX Sweep 1",
    "Juno B37 Sharp Reed",
    "Juno B38 Bass Pluck",
    "Juno B41 Resonant Rise",
    "Juno B42 Harpsichord 2",
    "Juno B43 Dark Ensemble",
    "Juno B44 Contact Wah",
    "Juno B45 Noise Sweep 2",
    "Juno B46 Glassy Wah",
    "Juno B47 Phase Ensemble",
    "Juno B48 Chorused Bell",
    "Juno B51 Clav",
    "Juno B52 Organ 2",
    "Juno B53 Bassoon",
    "Juno B54 Auto Release Noise Sweep",
    "Juno B55 Brass Ensemble",
    "Juno B56 Ethereal",
    "Juno B57 Chorus Bell 2",
    "Juno B58 Blizzard",
    "Juno B61 E. Piano with Tremolo",
    "Juno B62 Clarinet",
    "Juno B63 Thunder",
    "Juno B64 Reedy Organ",
    "Juno B65 Flute / Horn",
    "Juno B66 Toy Rhodes",
    "Juno B67 Surf's Up",
    "Juno B68 OW Bass",
    "Juno B71 Piccolo",
    "Juno B72 Melodic Taps",
    "Juno B73 Meow Brass",
    "Juno B74 Violin (high)",
    "Juno B75 High Bells",
    "Juno B76 Rolling Wah",
    "Juno B77 Ping Bell",
    "Juno B78 Brassy Organ",
    "Juno B81 Low Dark Strings",
    "Juno B82 Piccolo Trumpet",
    "Juno B83 Cello",
    "Juno B84 High Strings",
    "Juno B85 Rocket Men",
    "Juno B86 Forbidden Planet",
    "Juno B87 Froggy",
    "Juno B88 Owgan",
    "DX7 BRASS   1",
    "DX7 BRASS   2",
    "DX7 BRASS   3",
    "DX7 STRINGS 1",
    "DX7 STRINGS 2",
    "DX7 STRINGS 3",
    "DX7 ORCHESTRA",
    "DX7 PIANO   1",
    "DX7 PIANO   2",
    "DX7 PIANO   3",
    "DX7 E.PIANO 1",
    "DX7 GUITAR  1",
    "DX7 GUITAR  2",
    "DX7 SYN-LEAD 1",
    "DX7 BASS    1",
    "DX7 BASS    2",
    "DX7 E.ORGAN 1",
    "DX7 PIPES   1",
    "DX7 HARPSICH 1",
    "DX7 CLAV    1",
    "DX7 VIBE    1",
    "DX7 MARIMBA",
    "DX7 KOTO",
    "DX7 FLUTE   1",
    "DX7 ORCH-CHIME",
    "DX7 TUB BELLS",
    "DX7 STEEL DRUM",
    "DX7 TIMPANI",
    "DX7 REFS WHISL",
    "DX7 VOICE   1",
    "DX7 TRAIN",
    "DX7 TAKE OFF",
    "DX7 PIANO   4",
    "DX7 PIANO   5",
    "DX7 E.PIANO 2",
    "DX7 E.PIANO 3",
    "DX7 E.PIANO 4",
    "DX7 PIANO 5THS",
    "DX7 CELESTE",
    "DX7 TOY PIANO",
    "DX7 HARPSICH 2",
    "DX7 HARPSICH 3",
    "DX7 CLAV    2",
    "DX7 CLAV    3",
    "DX7 E.ORGAN 2",
    "DX7 E.ORGAN 3",
    "DX7 E.ORGAN 4",
    "DX7 E.ORGAN 5",
    "DX7 PIPES   2",
    "DX7 PIPES   3",
    "DX7 PIPES   4",
    "DX7 CALIOPE",
    "DX7 ACCORDION",
    "DX7 SITAR",
    "DX7 GUITAR  3",
    "DX7 GUITAR  4",
    "DX7 GUITAR  5",
    "DX7 GUITAR  6",
    "DX7 LUTE",
    "DX7 BANJO",
    "DX7 HARP    1",
    "DX7 HARP    2",
    "DX7 BASS    3",
    "DX7 BASS    4",
    "DX7 PICCOLO",
    "DX7 FLUTE   2",
    "DX7 OBOE",
    "DX7 CLARINET",
    "DX7 SAX BC",
    "DX7 BASSOON",
    "DX7 STRINGS 4",
    "DX7 STRINGS 5",
    "DX7 STRINGS 6",
    "DX7 STRINGS 7",
    "DX7 STRINGS 8",
    "DX7 BRASS   4",
    "DX7 BRASS   5",
    "DX7 BRASS 6 BC",
    "DX7 BRASS   7",
    "DX7 BRASS   8",
    "DX7 RECORDER",
    "DX7 HARMONICA1",
    "DX7 HRMNCA2 BC",
    "DX7 VOICE   2",
    "DX7 VOICE   3",
    "DX7 GLOKENSPL",
    "DX7 VIBE    2",
    "DX7 XYLOPHONE",
    "DX7 CHIMES",
    "DX7 GONG    1",
    "DX7 GONG    2",
    "DX7 BELLS",
    "DX7 COW BELL",
    "DX7 BLOCK",
    "DX7 FLEXATONE",
    "DX7 LOG DRUM",
    "DX7 SYN-LEAD 2",
    "DX7 SYN-LEAD 3",
    "DX7 SYN-LEAD 4",
    "DX7 SYN-LEAD 5",
    "DX7 SYN-CLAV 1",
    "DX7 SYN-CLAV 2",
    "DX7 SYN-CLAV 3",
    "DX7 SYN-PIANO",
    "DX7 SYNBRASS 1",
    "DX7 SYNBRASS 2",
    "DX7 SYNORGAN 1",
    "DX7 SYNORGAN 2",
    "DX7 SYN-VOX",
    "DX7 SYN-ORCH",
    "DX7 SYN-BASS 1",
    "DX7 SYN-BASS 2",
    "DX7 HARP-FLUTE",
    "DX7 BELL-FLUTE",
    "DX7 E.P-BRS BC",
    "DX7 T.BL-EXPA",
    "DX7 CHIME-STRG",
    "DX7 B.DRM-SNAR",
    "DX7 SHIMMER",
    "DX7 EVOLUTION",
    "DX7 WATER GDN",
    "DX7 WASP STING",
    "DX7 LASER GUN",
    "DX7 DESCENT",
    "DX7 OCTAVE WAR",
    "DX7 GRAND PRIX",
    "DX7 ST.HELENS",
    "DX7 EXPLOSION",
    "dpwe piano",
    "amyboard default",
};

typedef struct {
    uint16_t patch_number;
    int8_t octave_shift;
    bool key_down[PATCH_BROWSER_KEY_COUNT];
    uint8_t active_notes;
    synth_app_wheel_t volume_wheel;
    control_joystick_direction_t last_direction;
} patch_browser_state_t;

static uint8_t clamp_patch_voices(uint8_t voices)
{
    if (voices == 0) {
        return 1;
    }
    if (voices > 6) {
        return 6;
    }
    return voices;
}

static uint8_t patch_oscs_per_voice(uint16_t patch_number)
{
    if (patch_number < 128U) {
        return 5;
    }
    if (patch_number < 256U) {
        return 8;
    }
    if (patch_number == 256U) {
        return 25;
    }
    return 3;
}

static uint8_t patch_voice_count(uint16_t patch_number)
{
    if (patch_number < 128U) {
        return clamp_patch_voices(ESP_POCKET_AUDIO_AMY_JUNO_VOICES);
    }
    if (patch_number < 256U) {
        return clamp_patch_voices(ESP_POCKET_AUDIO_AMY_DX7_VOICES);
    }
    if (patch_number == 256U) {
        return clamp_patch_voices(ESP_POCKET_AUDIO_AMY_PIANO_VOICES);
    }
    return 6;
}

static const char *patch_bank_name(uint16_t patch_number)
{
    if (patch_number < 128U) {
        return "JUNO";
    }
    if (patch_number < 256U) {
        return "DX7";
    }
    if (patch_number == 256U) {
        return "PIANO";
    }
    return "AMY";
}

static void patch_short_label(uint16_t patch_number, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if (patch_number >= SYNTH_AMY_BUILTIN_PATCH_COUNT) {
        snprintf(out, out_size, "UNKNOWN");
        return;
    }

    snprintf(out, out_size, "%s", PATCH_NAMES[patch_number]);
}

static uint16_t wrapped_patch_number(int32_t patch_number)
{
    int32_t count = (int32_t)SYNTH_AMY_BUILTIN_PATCH_COUNT;
    while (patch_number < 0) {
        patch_number += count;
    }
    while (patch_number >= count) {
        patch_number -= count;
    }
    return (uint16_t)patch_number;
}

static uint8_t test_note_for_key(uint8_t key, int8_t octave_shift)
{
    if (key >= PATCH_BROWSER_KEY_COUNT) {
        key = 0;
    }

    int note = (int)TEST_NOTES[key] + ((int)octave_shift * 12);
    if (note < 0) {
        note = 0;
    } else if (note > 127) {
        note = 127;
    }
    return (uint8_t)note;
}

static void note_name(uint8_t midi_note, char *out, size_t out_size)
{
    static const char *const NAMES[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };

    if (out == NULL || out_size == 0) {
        return;
    }

    uint8_t note = midi_note % 12U;
    int octave = (int)(midi_note / 12U) - 1;
    snprintf(out, out_size, "%s%d", NAMES[note], octave);
}

static esp_err_t stop_all_notes(patch_browser_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t first_error = ESP_OK;
    for (uint8_t key = 0; key < PATCH_BROWSER_KEY_COUNT; ++key) {
        if (!state->key_down[key]) {
            continue;
        }

        uint8_t midi_note = test_note_for_key(key, state->octave_shift);
        esp_err_t err = synth_amy_builtin_patch_note_off(midi_note);
        if (err != ESP_OK && first_error == ESP_OK) {
            first_error = err;
        }
        state->key_down[key] = false;
    }
    state->active_notes = 0;
    return first_error;
}

static esp_err_t apply_patch_selection(patch_browser_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(stop_all_notes(state), TAG, "stop notes failed");

    uint8_t voices = patch_voice_count(state->patch_number);
    uint8_t oscs_per_voice = patch_oscs_per_voice(state->patch_number);
    ESP_RETURN_ON_ERROR(synth_amy_set_builtin_patch(state->patch_number,
                                                    voices,
                                                    oscs_per_voice),
                        TAG,
                        "set patch failed");

    char label[24];
    patch_short_label(state->patch_number, label, sizeof(label));
    ESP_LOGI(TAG,
             "selected patch=%u/%u %s bank=%s voices=%u oscs_per_voice=%u velocity=%.2f",
             (unsigned)state->patch_number,
             (unsigned)(SYNTH_AMY_BUILTIN_PATCH_COUNT - 1U),
             label,
             patch_bank_name(state->patch_number),
             (unsigned)voices,
             (unsigned)oscs_per_voice,
             PATCH_BROWSER_VELOCITY);
    return ESP_OK;
}

static esp_err_t play_key_note(patch_browser_state_t *state, uint8_t key)
{
    if (state == NULL || key >= PATCH_BROWSER_KEY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->key_down[key]) {
        return ESP_OK;
    }

    uint8_t midi_note = test_note_for_key(key, state->octave_shift);
    ESP_RETURN_ON_ERROR(synth_amy_builtin_patch_note_on(midi_note, PATCH_BROWSER_VELOCITY),
                        TAG,
                        "note on failed");
    state->key_down[key] = true;
    ++state->active_notes;
    return ESP_OK;
}

static esp_err_t stop_key_note(patch_browser_state_t *state, uint8_t key)
{
    if (state == NULL || key >= PATCH_BROWSER_KEY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state->key_down[key]) {
        return ESP_OK;
    }

    uint8_t midi_note = test_note_for_key(key, state->octave_shift);
    ESP_RETURN_ON_ERROR(synth_amy_builtin_patch_note_off(midi_note), TAG, "note off failed");
    state->key_down[key] = false;
    if (state->active_notes > 0) {
        --state->active_notes;
    }
    return ESP_OK;
}

static void update_volume_from_wheel(patch_browser_state_t *state)
{
    if (state == NULL) {
        return;
    }

    control_wheel_state_t wheel = {0};
    if (controls_wheel_read(&wheel) != ESP_OK || !wheel.present) {
        return;
    }

    uint8_t percent = 0;
    float volume = 0.0f;
    if (wheel.type == CONTROL_WHEEL_ABSOLUTE) {
        synth_app_wheel_update(&state->volume_wheel,
                               true,
                               wheel.data.absolute.percent);
        if (!synth_app_wheel_should_apply(&state->volume_wheel,
                                          SYNTH_APP_WHEEL_TARGET_VOLUME,
                                          wheel.data.absolute.percent,
                                          ESP_POCKET_AUDIO_WHEEL_STEP_PERCENT)) {
            return;
        }
        percent = wheel.data.absolute.percent;
        volume = wheel.data.absolute.value;
    } else if (wheel.type == CONTROL_WHEEL_RELATIVE) {
        int32_t delta = 0;
        synth_app_wheel_update_relative(&state->volume_wheel,
                                        true,
                                        wheel.data.relative.position,
                                        &delta);
        if (delta == 0) {
            return;
        }
        uint8_t current = synth_app_wheel_volume_percent(&state->volume_wheel);
        percent = synth_app_wheel_adjust_percent(current,
                                                 delta,
                                                 ESP_POCKET_AUDIO_WHEEL_STEP_PERCENT);
        if (percent == current) {
            return;
        }
        volume = (float)percent / 100.0f;
    } else {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_volume(volume));
    synth_app_wheel_set_volume_percent(&state->volume_wheel, percent);
}

static void draw_browser(const esp_pocket_audio_hw_t *hw, const patch_browser_state_t *state)
{
    if (hw == NULL || state == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    char line[24];
    char label[24];
    char note[8];
    patch_short_label(state->patch_number, label, sizeof(label));
    note_name(test_note_for_key(0, state->octave_shift), note, sizeof(note));

    uint8_t voices = patch_voice_count(state->patch_number);
    uint8_t oscs_per_voice = patch_oscs_per_voice(state->patch_number);

    pocket_oled_t *display = (pocket_oled_t *)&hw->display;
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "AMY PATCH BROWSER");

    snprintf(line,
             sizeof(line),
             "P%03u/%03u %.7s",
             (unsigned)state->patch_number,
             (unsigned)(SYNTH_AMY_BUILTIN_PATCH_COUNT - 1U),
             patch_bank_name(state->patch_number));
    pocket_oled_draw_text_row(display, 1, line);

    snprintf(line, sizeof(line), "%.21s", label);
    pocket_oled_draw_text_row(display, 2, line);

    snprintf(line,
             sizeof(line),
             "OSC %02u VOI %u VEL %.2f",
             (unsigned)oscs_per_voice,
             (unsigned)voices,
             PATCH_BROWSER_VELOCITY);
    pocket_oled_draw_text_row(display, 3, line);

    snprintf(line,
             sizeof(line),
             "%c%u %s VOL %3u%%",
             state->active_notes == 0 ? 'S' : 'P',
             (unsigned)state->active_notes,
             note,
             (unsigned)synth_app_wheel_volume_percent(&state->volume_wheel));
    pocket_oled_draw_text_row(display, 4, line);

    snprintf(line, sizeof(line), "OCT %+d 1-7 PLAY", (int)state->octave_shift);
    pocket_oled_draw_text_row(display, 5, line);
    pocket_oled_draw_text_row(display, 6, "L/R +/-1 U/D +/-8");
    pocket_oled_draw_text_row(display, 7, "M OCT J STOP K EXIT");

    esp_err_t err = oled_display_flush_cooperatively(display, UI_PAGE_TIMEOUT_TICKS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED flush skipped: %s", esp_err_to_name(err));
    }
}

static void step_patch(patch_browser_state_t *state, int8_t steps)
{
    if (state == NULL) {
        return;
    }

    state->patch_number = wrapped_patch_number((int32_t)state->patch_number + steps);
    esp_err_t err = apply_patch_selection(state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "patch select failed: %s", esp_err_to_name(err));
    }
}

static void step_octave(patch_browser_state_t *state)
{
    if (state == NULL) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(stop_all_notes(state));
    int value = (int)state->octave_shift + 1;
    if (value > 2) {
        value = -2;
    }
    state->octave_shift = (int8_t)value;
}

static bool handle_joystick(patch_browser_state_t *state)
{
    control_joystick_state_t joystick = {0};
    if (state == NULL || controls_joystick_read(&joystick) != ESP_OK) {
        return false;
    }

    if (joystick.direction == state->last_direction) {
        return false;
    }
    state->last_direction = joystick.direction;

    switch (joystick.direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_patch(state, -1);
        return true;
    case CONTROL_JOYSTICK_RIGHT:
        step_patch(state, 1);
        return true;
    case CONTROL_JOYSTICK_UP:
        step_patch(state, PATCH_BROWSER_BANK_STEP);
        return true;
    case CONTROL_JOYSTICK_DOWN:
        step_patch(state, -(int8_t)PATCH_BROWSER_BANK_STEP);
        return true;
    case CONTROL_JOYSTICK_CENTER:
    default:
        return false;
    }
}

esp_err_t diagnostics_amy_patch_browser_run(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "AMY patch browser started");
    ESP_RETURN_ON_ERROR(synth_amy_init(hw), TAG, "AMY init failed");

    patch_browser_state_t state = {
        .patch_number = 0,
        .octave_shift = 0,
        .active_notes = 0,
        .volume_wheel = SYNTH_APP_WHEEL_INITIALIZER(ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT),
        .last_direction = CONTROL_JOYSTICK_CENTER,
    };
    ESP_RETURN_ON_ERROR(apply_patch_selection(&state), TAG, "initial patch failed");

    TickType_t next_refresh = 0;
    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool dirty = false;

        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            bool chord_key_event = false;
            for (uint8_t key = 0; key < PATCH_BROWSER_KEY_COUNT; ++key) {
                if (event.sw != CHORD_KEYS[key]) {
                    continue;
                }

                chord_key_event = true;
                esp_err_t err = ESP_OK;
                if (event.type == CONTROL_EVENT_SWITCH_PRESSED) {
                    err = play_key_note(&state, key);
                } else {
                    err = stop_key_note(&state, key);
                }
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "key note event failed: %s", esp_err_to_name(err));
                }
                dirty = true;
                break;
            }
            if (chord_key_event || event.type != CONTROL_EVENT_SWITCH_PRESSED) {
                continue;
            }

            if (event.sw == CONTROL_JOYSTICK_CLICK) {
                esp_err_t err = stop_all_notes(&state);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "stop notes failed: %s", esp_err_to_name(err));
                }
                dirty = true;
            } else if (event.sw == CONTROL_MODE_MENU) {
                step_octave(&state);
                dirty = true;
            } else if (event.sw == CONTROL_KEY_MENU) {
                ESP_LOGI(TAG, "leaving AMY patch browser");
                ESP_ERROR_CHECK_WITHOUT_ABORT(stop_all_notes(&state));
                ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
                return ESP_OK;
            }
        }

        dirty = handle_joystick(&state) || dirty;
        update_volume_from_wheel(&state);
        if ((int32_t)(now - next_refresh) >= 0 || dirty) {
            draw_browser(hw, &state);
            next_refresh = now + UI_REFRESH_TICKS;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
