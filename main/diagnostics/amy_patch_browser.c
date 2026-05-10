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
#include "synth/chord_engine.h"
#include "synth/sound.h"

static const char *TAG = "DIAG_AMY_PATCH";

static const TickType_t UI_REFRESH_TICKS = pdMS_TO_TICKS(100);
static const TickType_t UI_PAGE_TIMEOUT_TICKS = pdMS_TO_TICKS(25);
static const uint8_t TEST_SLOT = 0;
static const uint8_t VOLUME_CHANGE_THRESHOLD_PERCENT = 2;

typedef struct {
    synth_sound_t sound;
    int8_t octave_shift;
    bool playing;
    uint8_t volume_percent;
    uint8_t last_applied_volume_percent;
    control_joystick_direction_t last_direction;
} patch_browser_state_t;

static void build_test_chord(int8_t octave_shift, synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    uint8_t root = (uint8_t)(SYNTH_DEFAULT_ROOT_MIDI + (octave_shift * 12));
    *chord = (synth_chord_t) {
        .notes = {root, (uint8_t)(root + 4U), (uint8_t)(root + 7U)},
        .note_count = 3,
        .degree = 0,
        .root_midi = root,
        .key = SYNTH_KEY_C,
        .scale = SYNTH_SCALE_MAJOR,
        .modifier = SYNTH_CHORD_MOD_NONE,
    };
    snprintf(chord->name, sizeof(chord->name), "C");
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
    state->playing = false;
    ESP_LOGI(TAG,
             "selected %u/%u %s engine=%s velocity=%.2f",
             (unsigned)(state->sound + 1),
             (unsigned)SYNTH_SOUND_COUNT,
             synth_sound_name(state->sound),
             synth_sound_engine_name(state->sound),
             synth_amy_sound_velocity(state->sound));
    return ESP_OK;
}

static esp_err_t toggle_test_chord(patch_browser_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (state->playing) {
        ESP_RETURN_ON_ERROR(synth_amy_chord_off(TEST_SLOT), TAG, "chord off failed");
        state->playing = false;
        return ESP_OK;
    }

    synth_chord_t chord = {0};
    build_test_chord(state->octave_shift, &chord);
    ESP_RETURN_ON_ERROR(synth_amy_chord_on(TEST_SLOT, &chord), TAG, "chord on failed");
    state->playing = true;
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

    synth_chord_t chord = {0};
    build_test_chord(state->octave_shift, &chord);
    char note[8];
    note_name(chord.root_midi, note, sizeof(note));

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
             state->playing ? "PLAY" : "STOP",
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
    pocket_oled_draw_text_row(display, 7, "J PLAY/STOP K EXIT");

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
    if (state->playing) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(TEST_SLOT));
        state->playing = false;
        ESP_ERROR_CHECK_WITHOUT_ABORT(toggle_test_chord(state));
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
        .playing = false,
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
            if (event.type != CONTROL_EVENT_SWITCH_PRESSED) {
                continue;
            }

            if (event.sw == CONTROL_JOYSTICK_CLICK) {
                esp_err_t err = toggle_test_chord(&state);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "test chord failed: %s", esp_err_to_name(err));
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
