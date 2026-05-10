#include "diagnostics/amy_patch_browser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"
#include "synth/amy_engine.h"
#include "synth/sound.h"

static const char *TAG = "DIAG_AMY_PATCH";

static const TickType_t UI_REFRESH_TICKS = pdMS_TO_TICKS(100);
static const TickType_t UI_PAGE_TIMEOUT_TICKS = pdMS_TO_TICKS(25);
static const uint8_t VOLUME_CHANGE_THRESHOLD_PERCENT = 2;
static const uint8_t NO_ACTIVE_KEY = UINT8_MAX;
static const uint8_t TEST_NOTES[SYNTH_CHORD_BUTTON_COUNT] = {
    60, 62, 64, 65, 67, 69, 71,
};
static const control_switch_t CHORD_KEYS[SYNTH_CHORD_BUTTON_COUNT] = {
    CONTROL_CHORD_1,
    CONTROL_CHORD_2,
    CONTROL_CHORD_3,
    CONTROL_CHORD_4,
    CONTROL_CHORD_5,
    CONTROL_CHORD_6,
    CONTROL_CHORD_7,
};

typedef struct {
    synth_sound_t sound;
    int8_t octave_shift;
    uint8_t active_key;
    uint8_t volume_percent;
    uint8_t last_applied_volume_percent;
    control_joystick_direction_t last_direction;
} patch_browser_state_t;

static uint8_t test_note_for_key(uint8_t key, int8_t octave_shift)
{
    if (key >= SYNTH_CHORD_BUTTON_COUNT) {
        key = 0;
    }
    return (uint8_t)(TEST_NOTES[key] + (octave_shift * 12));
}

static void build_test_note(uint8_t midi_note, synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    *chord = (synth_chord_t) {
        .notes = {midi_note},
        .note_count = 1,
        .root_midi = midi_note,
    };
    snprintf(chord->name, sizeof(chord->name), "NOTE");
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

static uint16_t velocity_centi(synth_sound_t sound)
{
    float velocity = synth_amy_sound_velocity(sound);
    if (velocity < 0.0f) {
        velocity = 0.0f;
    } else if (velocity > 9.99f) {
        velocity = 9.99f;
    }
    return (uint16_t)((velocity * 100.0f) + 0.5f);
}

static esp_err_t apply_sound(patch_browser_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(synth_amy_all_off(), TAG, "all off failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_sound(state->sound), TAG, "set sound failed");
    state->active_key = NO_ACTIVE_KEY;
    ESP_LOGI(TAG,
             "selected %u/%u %s engine=%s velocity=%.2f",
             (unsigned)(state->sound + 1),
             (unsigned)SYNTH_SOUND_COUNT,
             synth_sound_name(state->sound),
             synth_sound_engine_name(state->sound),
             synth_amy_sound_velocity(state->sound));
    return ESP_OK;
}

static esp_err_t stop_active_note(patch_browser_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (state->active_key != NO_ACTIVE_KEY) {
        ESP_RETURN_ON_ERROR(synth_amy_chord_off(state->active_key), TAG, "note off failed");
        state->active_key = NO_ACTIVE_KEY;
    }
    return ESP_OK;
}

static esp_err_t play_key_note(patch_browser_state_t *state, uint8_t key)
{
    if (state == NULL || key >= SYNTH_CHORD_BUTTON_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t midi_note = test_note_for_key(key, state->octave_shift);
    synth_chord_t chord = {0};
    build_test_note(midi_note, &chord);
    ESP_RETURN_ON_ERROR(synth_amy_chord_on(key, &chord), TAG, "note on failed");
    state->active_key = key;
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

    state->volume_percent = wheel.percent;
    uint8_t last = state->last_applied_volume_percent;
    uint8_t current = wheel.percent;
    uint8_t delta = current > last ? (uint8_t)(current - last) : (uint8_t)(last - current);
    if (last != UINT8_MAX && delta < VOLUME_CHANGE_THRESHOLD_PERCENT) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_volume(wheel.value));
    state->last_applied_volume_percent = current;
}

static void draw_browser(const esp_pocket_audio_hw_t *hw, const patch_browser_state_t *state)
{
    if (hw == NULL || state == NULL || !pocket_oled_is_initialized(&hw->display)) {
        return;
    }

    uint16_t patch_number = 0;
    const char *patch_label = NULL;
    bool has_patch = synth_amy_sound_patch_info(state->sound, &patch_number, &patch_label);

    char note[8];
    uint8_t display_note = state->active_key == NO_ACTIVE_KEY ?
                           test_note_for_key(0, state->octave_shift) :
                           test_note_for_key(state->active_key, state->octave_shift);
    note_name(display_note, note, sizeof(note));

    char line[24];
    pocket_oled_t *display = (pocket_oled_t *)&hw->display;
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "AMY PATCH BROWSER");

    snprintf(line,
             sizeof(line),
             "%02u/%02u %.14s",
             (unsigned)(state->sound + 1),
             (unsigned)SYNTH_SOUND_COUNT,
             synth_sound_name(state->sound));
    pocket_oled_draw_text_row(display, 1, line);

    snprintf(line, sizeof(line), "ENG %.17s", synth_sound_engine_name(state->sound));
    pocket_oled_draw_text_row(display, 2, line);

    if (has_patch) {
        uint16_t velocity = velocity_centi(state->sound);
        snprintf(line,
                 sizeof(line),
                 "P%03u VEL %u.%02u",
                 (unsigned)patch_number,
                 (unsigned)(velocity / 100U),
                 (unsigned)(velocity % 100U));
    } else {
        uint16_t velocity = velocity_centi(state->sound);
        snprintf(line,
                 sizeof(line),
                 "NO PATCH VEL %u.%02u",
                 (unsigned)(velocity / 100U),
                 (unsigned)(velocity % 100U));
    }
    pocket_oled_draw_text_row(display, 3, line);

    snprintf(line,
             sizeof(line),
             "%s %s VOL %3u%%",
             state->active_key == NO_ACTIVE_KEY ? "STOP" : "PLAY",
             note,
             (unsigned)state->volume_percent);
    pocket_oled_draw_text_row(display, 4, line);

    if (patch_label != NULL) {
        snprintf(line, sizeof(line), "%.21s", patch_label);
    } else {
        snprintf(line, sizeof(line), "J PLAY  K EXIT");
    }
    pocket_oled_draw_text_row(display, 5, line);
    pocket_oled_draw_text_row(display, 6, "L/R SOUND U/D OCT");
    pocket_oled_draw_text_row(display, 7, "1-7 PLAY J STOP K");

    esp_err_t err = oled_display_flush_cooperatively(display, UI_PAGE_TIMEOUT_TICKS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED flush skipped: %s", esp_err_to_name(err));
    }
}

static void step_sound(patch_browser_state_t *state, int8_t steps)
{
    if (state == NULL) {
        return;
    }

    state->sound = synth_sound_step(state->sound, steps);
    esp_err_t err = apply_sound(state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sound select failed: %s", esp_err_to_name(err));
    }
}

static void step_octave(patch_browser_state_t *state, int8_t steps)
{
    if (state == NULL) {
        return;
    }

    int value = (int)state->octave_shift + steps;
    if (value < -2) {
        value = -2;
    } else if (value > 2) {
        value = 2;
    }
    state->octave_shift = (int8_t)value;
    if (state->active_key != NO_ACTIVE_KEY) {
        uint8_t key = state->active_key;
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_active_note(state));
        ESP_ERROR_CHECK_WITHOUT_ABORT(play_key_note(state, key));
    }
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
        step_sound(state, -1);
        return true;
    case CONTROL_JOYSTICK_RIGHT:
        step_sound(state, 1);
        return true;
    case CONTROL_JOYSTICK_UP:
        step_octave(state, 1);
        return true;
    case CONTROL_JOYSTICK_DOWN:
        step_octave(state, -1);
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
        .sound = SYNTH_SOUND_SAW,
        .octave_shift = 0,
        .active_key = NO_ACTIVE_KEY,
        .volume_percent = 100,
        .last_applied_volume_percent = UINT8_MAX,
        .last_direction = CONTROL_JOYSTICK_CENTER,
    };
    ESP_RETURN_ON_ERROR(apply_sound(&state), TAG, "initial sound failed");

    TickType_t next_refresh = 0;
    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool dirty = false;

        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            bool chord_key_event = false;
            for (uint8_t key = 0; key < SYNTH_CHORD_BUTTON_COUNT; ++key) {
                if (event.sw != CHORD_KEYS[key]) {
                    continue;
                }

                chord_key_event = true;
                if (event.type == CONTROL_EVENT_SWITCH_PRESSED) {
                    esp_err_t err = play_key_note(&state, key);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "key note failed: %s", esp_err_to_name(err));
                    }
                } else if (state.active_key == key) {
                    esp_err_t err = stop_active_note(&state);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "key note off failed: %s", esp_err_to_name(err));
                    }
                }
                dirty = true;
                break;
            }
            if (chord_key_event || event.type != CONTROL_EVENT_SWITCH_PRESSED) {
                continue;
            }

            if (event.sw == CONTROL_JOYSTICK_CLICK) {
                esp_err_t err = stop_active_note(&state);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "stop note failed: %s", esp_err_to_name(err));
                }
                dirty = true;
            } else if (event.sw == CONTROL_KEY_MENU) {
                ESP_LOGI(TAG, "leaving AMY patch browser");
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
