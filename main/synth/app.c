#include "synth/app.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "hmi/play_ui.h"
#include "synth/amy_engine.h"
#include "synth/chord_engine.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_joystick_mode_t joystick_mode;
    synth_sound_t sound;
    synth_play_mode_t mode;
    uint16_t bpm;
    uint8_t effect_index;
    bool effects[PLAY_UI_EFFECT_COUNT];
    uint8_t lfo_level;
    uint8_t delay_level;
    uint8_t tremolo_level;
    float lfo_phase;
    TickType_t lfo_last_tick;
    float tremolo_phase;
    TickType_t tremolo_last_tick;
    bool wheel_present;
    uint8_t wheel_percent;
    uint8_t last_applied_wheel_percent;
    control_joystick_direction_t last_menu_direction;
    control_joystick_direction_t last_play_direction;
    control_joystick_direction_t joystick_direction;
    uint8_t held_mask;
    uint8_t focus_slot;
    synth_chord_t arp_chords[SYNTH_CHORD_BUTTON_COUNT];
    bool arp_chord_valid[SYNTH_CHORD_BUTTON_COUNT];
    uint8_t arp_note_index;
    uint8_t arp_last_slot;
    TickType_t arp_next_tick;
    char chord_name[16];
    char status[22];
    TaskHandle_t task;
} synth_app_state_t;

static const char *TAG = "SYNTH_APP";

static const uint32_t APP_TASK_STACK_SIZE = 4096;
static const UBaseType_t APP_TASK_PRIORITY = 5;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t APP_UPDATE_TICKS = pdMS_TO_TICKS(20);
static const int8_t GLOBAL_OCTAVE_MIN = -2;
static const int8_t GLOBAL_OCTAVE_MAX = 2;
static const int8_t BUTTON_OCTAVE_MIN = -2;
static const int8_t BUTTON_OCTAVE_MAX = 1;
static const uint16_t BPM_MIN = 40;
static const uint16_t BPM_MAX = 300;
static const uint8_t WHEEL_CHANGE_THRESHOLD_PERCENT = 2;
static const float LFO_RATE_HZ = 5.0f;
static const float LFO_TWO_PI = 6.283185307f;
static const float LFO_DEPTH_SEMITONES[] = {
    0.0f, 0.12f, 0.35f, 0.75f,
};
static const char *DELAY_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T",
};
static const float DELAY_LEVEL_DIVISORS[] = {
    0.0f, 1.0f, 2.0f, 4.0f, 6.0f,
};
static const char *TREMOLO_LEVEL_NAMES[] = {
    "OFF", "1/4", "1/8", "1/16", "1/16T", "1/32",
};
static const float TREMOLO_LEVEL_DIVISORS[] = {
    0.0f, 1.0f, 2.0f, 4.0f, 6.0f, 8.0f,
};
static const float TREMOLO_DEPTH = 0.78f;
static const uint8_t ARP_NO_SLOT = UINT8_MAX;

static synth_app_state_t s_app = {
    .key = SYNTH_KEY_C,
    .scale = SYNTH_SCALE_MAJOR,
    .global_octave_shift = 0,
    .modifier = SYNTH_CHORD_MOD_NONE,
    .joystick_mode = SYNTH_JOYSTICK_MODE_DEFAULT,
    .sound = SYNTH_SOUND_SAW,
    .mode = SYNTH_PLAY_MODE_PLAY,
    .bpm = 120,
    .effect_index = PLAY_UI_EFFECT_JOYSTICK,
    .effects = {
        [PLAY_UI_EFFECT_FILTER] = true,
    },
    .wheel_percent = 100,
    .last_applied_wheel_percent = UINT8_MAX,
    .last_menu_direction = CONTROL_JOYSTICK_CENTER,
    .last_play_direction = CONTROL_JOYSTICK_CENTER,
    .joystick_direction = CONTROL_JOYSTICK_CENTER,
    .focus_slot = 0,
    .arp_last_slot = UINT8_MAX,
    .chord_name = "---",
    .status = "READY",
};

static play_ui_screen_t active_screen(void)
{
    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        return PLAY_UI_SCREEN_KEY;
    }
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        return PLAY_UI_SCREEN_SOUND;
    }
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        return PLAY_UI_SCREEN_MODE;
    }
    return PLAY_UI_SCREEN_HOME;
}

static void set_status(const char *status)
{
    if (status != NULL) {
        strlcpy(s_app.status, status, sizeof(s_app.status));
    }
}

static void update_ui(void)
{
    play_ui_model_t model = {0};
    play_ui_model_init(&model);
    model.screen = active_screen();
    model.key = s_app.key;
    model.scale = s_app.scale;
    model.global_octave_shift = s_app.global_octave_shift;
    memcpy(model.button_octave_shift, s_app.button_octave_shift, sizeof(model.button_octave_shift));
    model.modifier = s_app.modifier;
    model.joystick_mode = s_app.joystick_mode;
    model.sound = s_app.sound;
    model.mode = s_app.mode;
    model.bpm = s_app.bpm;
    model.held_mask = s_app.held_mask;
    model.focus_slot = s_app.focus_slot;
    model.effect_index = s_app.effect_index;
    memcpy(model.effects, s_app.effects, sizeof(model.effects));
    model.lfo_level = s_app.lfo_level;
    model.delay_level = s_app.delay_level;
    model.tremolo_level = s_app.tremolo_level;
    model.wheel_present = s_app.wheel_present;
    model.wheel_percent = s_app.wheel_percent;
    model.joystick_direction = s_app.joystick_direction;
    strlcpy(model.chord_name, s_app.chord_name, sizeof(model.chord_name));
    strlcpy(model.status, s_app.status, sizeof(model.status));
    play_ui_update(&model);
}

static uint8_t slot_from_degree(uint8_t degree)
{
    return degree > 0 ? (uint8_t)(degree - 1) : 0;
}

static bool build_chord(uint8_t degree, synth_chord_t *chord)
{
    uint8_t slot = slot_from_degree(degree);
    synth_chord_request_t request = {
        .key = s_app.key,
        .scale = s_app.scale,
        .degree = degree,
        .global_octave_shift = s_app.global_octave_shift,
        .button_octave_shift = s_app.button_octave_shift[slot],
        .modifier = s_app.modifier,
    };
    return synth_build_chord(&request, chord) == 0;
}

static int midi_note_octave(uint8_t midi_note)
{
    return ((int)midi_note / 12) - 1;
}

static void format_chord_notes(const synth_chord_t *chord, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (chord == NULL) {
        return;
    }

    size_t used = 0;
    for (uint8_t i = 0; i < chord->note_count && used < out_size; ++i) {
        uint8_t midi_note = chord->notes[i];
        int written = snprintf(out + used,
                               out_size - used,
                               "%s%s%d(%u)",
                               i == 0 ? "" : " ",
                               synth_key_name((synth_key_t)(midi_note % 12)),
                               midi_note_octave(midi_note),
                               midi_note);
        if (written < 0) {
            out[used] = '\0';
            return;
        }
        if ((size_t)written >= out_size - used) {
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

static TickType_t arp_step_ticks(void)
{
    uint32_t step_ms = 30000UL / s_app.bpm; /* Default 1/8 rate: two notes per beat. */
    if (step_ms < 20) {
        step_ms = 20;
    }
    return pdMS_TO_TICKS(step_ms);
}

static uint8_t first_held_slot(void)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) != 0 && s_app.arp_chord_valid[slot]) {
            return slot;
        }
    }
    return ARP_NO_SLOT;
}

static uint8_t active_arp_slot(void)
{
    if ((s_app.held_mask & (1U << s_app.focus_slot)) != 0 &&
        s_app.arp_chord_valid[s_app.focus_slot]) {
        return s_app.focus_slot;
    }
    return first_held_slot();
}

static void stop_arp(void)
{
    if (s_app.arp_last_slot != ARP_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.arp_last_slot));
    }
    s_app.arp_last_slot = ARP_NO_SLOT;
    s_app.arp_note_index = 0;
}

static void clear_arp_sources(void)
{
    memset(s_app.arp_chord_valid, 0, sizeof(s_app.arp_chord_valid));
    s_app.arp_note_index = 0;
    s_app.arp_next_tick = xTaskGetTickCount();
}

static void update_arp(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_ARP) {
        return;
    }

    uint8_t slot = active_arp_slot();
    if (slot == ARP_NO_SLOT) {
        stop_arp();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.arp_next_tick) < 0) {
        return;
    }

    const synth_chord_t *source = &s_app.arp_chords[slot];
    if (source->note_count == 0) {
        return;
    }

    if (s_app.arp_last_slot != ARP_NO_SLOT && s_app.arp_last_slot != slot) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.arp_last_slot));
    }
    if (s_app.arp_note_index >= source->note_count) {
        s_app.arp_note_index = 0;
    }

    synth_chord_t note = *source;
    note.notes[0] = source->notes[s_app.arp_note_index];
    note.note_count = 1;
    ++s_app.arp_note_index;

    esp_err_t err = synth_amy_chord_on(slot, &note);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play arp note: %s", esp_err_to_name(err));
        return;
    }

    s_app.arp_last_slot = slot;
    s_app.arp_next_tick = now + arp_step_ticks();

    char note_name[24];
    format_chord_notes(&note, note_name, sizeof(note_name));
    ESP_LOGD(TAG, "arp %s slot=%u note=%s", source->name, (unsigned)(slot + 1), note_name);
}

static void update_lfo(void)
{
    size_t level_count = sizeof(LFO_DEPTH_SEMITONES) / sizeof(LFO_DEPTH_SEMITONES[0]);
    if (s_app.lfo_level == 0 || s_app.lfo_level >= level_count) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_app.lfo_last_tick == 0) {
        s_app.lfo_last_tick = now;
        return;
    }

    TickType_t elapsed_ticks = now - s_app.lfo_last_tick;
    s_app.lfo_last_tick = now;
    if (elapsed_ticks == 0) {
        return;
    }

    float elapsed_seconds = ((float)elapsed_ticks * (float)portTICK_PERIOD_MS) / 1000.0f;
    s_app.lfo_phase += LFO_TWO_PI * LFO_RATE_HZ * elapsed_seconds;
    while (s_app.lfo_phase >= LFO_TWO_PI) {
        s_app.lfo_phase -= LFO_TWO_PI;
    }

    float pitch_semitones = sinf(s_app.lfo_phase) * LFO_DEPTH_SEMITONES[s_app.lfo_level];
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_lfo_pitch(pitch_semitones));
}

static float tremolo_rate_hz_for_level(uint8_t level)
{
    size_t level_count = sizeof(TREMOLO_LEVEL_DIVISORS) / sizeof(TREMOLO_LEVEL_DIVISORS[0]);
    if (level == 0 || level >= level_count) {
        return 0.0f;
    }

    return ((float)s_app.bpm / 60.0f) * TREMOLO_LEVEL_DIVISORS[level];
}

static void update_tremolo(void)
{
    size_t level_count = sizeof(TREMOLO_LEVEL_DIVISORS) / sizeof(TREMOLO_LEVEL_DIVISORS[0]);
    if (s_app.tremolo_level == 0 || s_app.tremolo_level >= level_count) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_app.tremolo_last_tick == 0) {
        s_app.tremolo_last_tick = now;
        return;
    }

    TickType_t elapsed_ticks = now - s_app.tremolo_last_tick;
    s_app.tremolo_last_tick = now;
    if (elapsed_ticks == 0) {
        return;
    }

    float elapsed_seconds = ((float)elapsed_ticks * (float)portTICK_PERIOD_MS) / 1000.0f;
    s_app.tremolo_phase += LFO_TWO_PI * tremolo_rate_hz_for_level(s_app.tremolo_level) * elapsed_seconds;
    while (s_app.tremolo_phase >= LFO_TWO_PI) {
        s_app.tremolo_phase -= LFO_TWO_PI;
    }

    float amount = TREMOLO_DEPTH * (0.5f + 0.5f * sinf(s_app.tremolo_phase));
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_tremolo_amount(amount));
}

static void play_degree(uint8_t degree)
{
    synth_chord_t chord = {0};
    if (!build_chord(degree, &chord)) {
        ESP_LOGW(TAG, "failed to build chord degree=%u", degree);
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    if (s_app.mode == SYNTH_PLAY_MODE_ARP) {
        s_app.arp_chords[slot] = chord;
        s_app.arp_chord_valid[slot] = true;
        s_app.arp_note_index = 0;
        s_app.arp_next_tick = xTaskGetTickCount();
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        set_status("ARP");
        char note_names[96];
        format_chord_notes(&chord, note_names, sizeof(note_names));
        ESP_LOGI(TAG, "arp source %s key=%s octave=%d mod=%s notes=%u [%s]",
                 chord.name,
                 synth_key_name(s_app.key),
                 s_app.global_octave_shift,
                 synth_chord_modifier_name(s_app.modifier),
                 chord.note_count,
                 note_names);
        update_ui();
        return;
    }

    esp_err_t err = synth_amy_chord_on(slot, &chord);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play chord: %s", esp_err_to_name(err));
        return;
    }

    char note_names[96];
    format_chord_notes(&chord, note_names, sizeof(note_names));
    ESP_LOGI(TAG, "play %s key=%s octave=%d mod=%s notes=%u [%s]",
             chord.name,
             synth_key_name(s_app.key),
             s_app.global_octave_shift,
             synth_chord_modifier_name(s_app.modifier),
             chord.note_count,
             note_names);
    strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
    set_status("PLAYING");
    update_ui();
}

static void refresh_held_chords(void)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) != 0) {
            play_degree((uint8_t)(slot + 1));
        }
    }
}

static void release_degree(uint8_t degree)
{
    uint8_t slot = slot_from_degree(degree);
    s_app.held_mask &= (uint8_t)~(1U << slot);
    s_app.arp_chord_valid[slot] = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(slot));
    if (s_app.held_mask == 0) {
        stop_arp();
        set_status("READY");
    }
    update_ui();
}

static void handle_chord_event(const control_event_t *event)
{
    uint8_t degree = 0;
    if (!keymap_switch_to_chord_degree(event->sw, &degree)) {
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    if (event->type == CONTROL_EVENT_SWITCH_PRESSED) {
        if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
            s_app.sound = synth_sound_quick_select(degree);
            ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(s_app.sound));
            set_status("SOUND SELECTED");
            refresh_held_chords();
            update_ui();
            return;
        }

        s_app.held_mask |= 1U << slot;
        s_app.focus_slot = slot;
        play_degree(degree);
    } else {
        release_degree(degree);
    }
}

static void step_key(int8_t semitones)
{
    s_app.key = synth_key_step(s_app.key, semitones);
    ESP_LOGI(TAG, "key=%s", synth_key_name(s_app.key));
    set_status("KEY CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_global_octave(int8_t octaves)
{
    int8_t next = (int8_t)(s_app.global_octave_shift + octaves);
    if (next < GLOBAL_OCTAVE_MIN) {
        next = GLOBAL_OCTAVE_MIN;
    } else if (next > GLOBAL_OCTAVE_MAX) {
        next = GLOBAL_OCTAVE_MAX;
    }

    if (next == s_app.global_octave_shift) {
        return;
    }

    s_app.global_octave_shift = next;
    ESP_LOGI(TAG, "global octave=%d", s_app.global_octave_shift);
    set_status("OCTAVE CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_focused_button_octave(int8_t octaves)
{
    if ((s_app.held_mask & (1U << s_app.focus_slot)) == 0) {
        return;
    }

    int8_t next = (int8_t)(s_app.button_octave_shift[s_app.focus_slot] + octaves);
    if (next < BUTTON_OCTAVE_MIN) {
        next = BUTTON_OCTAVE_MIN;
    } else if (next > BUTTON_OCTAVE_MAX) {
        next = BUTTON_OCTAVE_MAX;
    }

    if (next == s_app.button_octave_shift[s_app.focus_slot]) {
        return;
    }

    s_app.button_octave_shift[s_app.focus_slot] = next;
    ESP_LOGI(TAG, "chord %u octave=%d", (unsigned)(s_app.focus_slot + 1), next);
    set_status("BUTTON OCTAVE");
    play_degree((uint8_t)(s_app.focus_slot + 1));
    update_ui();
}

static void update_key_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_key(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_key(1);
        break;
    case CONTROL_JOYSTICK_UP:
        if (s_app.held_mask != 0) {
            step_focused_button_octave(1);
        } else {
            step_global_octave(1);
        }
        break;
    case CONTROL_JOYSTICK_DOWN:
        if (s_app.held_mask != 0) {
            step_focused_button_octave(-1);
        } else {
            step_global_octave(-1);
        }
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void step_sound(int8_t steps)
{
    s_app.sound = synth_sound_step(s_app.sound, steps);
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(s_app.sound));
    set_status("SOUND CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_effect(int8_t steps)
{
    int value = (int)s_app.effect_index + steps;
    while (value < 0) {
        value += PLAY_UI_EFFECT_COUNT;
    }
    s_app.effect_index = (uint8_t)(value % PLAY_UI_EFFECT_COUNT);
    set_status("EFFECT SELECT");
    update_ui();
}

static void step_joystick_mode(void)
{
    s_app.joystick_mode = synth_joystick_mode_step(s_app.joystick_mode, 1);
    s_app.modifier = synth_chord_modifier_from_joystick(s_app.joystick_mode, s_app.joystick_direction);
    ESP_LOGI(TAG, "joystick mode=%s", synth_joystick_mode_name(s_app.joystick_mode));
    set_status(synth_joystick_mode_name(s_app.joystick_mode));
    refresh_held_chords();
    update_ui();
}

static void step_lfo_level(void)
{
    size_t level_count = sizeof(LFO_DEPTH_SEMITONES) / sizeof(LFO_DEPTH_SEMITONES[0]);
    s_app.lfo_level = (uint8_t)((s_app.lfo_level + 1U) % level_count);
    s_app.effects[PLAY_UI_EFFECT_LFO] = s_app.lfo_level > 0;
    s_app.lfo_phase = 0.0f;
    s_app.lfo_last_tick = xTaskGetTickCount();

    esp_err_t err = synth_amy_set_lfo_enabled(s_app.effects[PLAY_UI_EFFECT_LFO]);
    if (err == ESP_OK) {
        err = synth_amy_set_lfo_pitch(0.0f);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply LFO: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG,
             "lfo=%s level=%u depth=%.2f semitones",
             s_app.effects[PLAY_UI_EFFECT_LFO] ? "on" : "off",
             s_app.lfo_level,
             LFO_DEPTH_SEMITONES[s_app.lfo_level]);
    set_status(s_app.effects[PLAY_UI_EFFECT_LFO] ? "LFO ON" : "LFO OFF");
    update_ui();
}

static float delay_ms_for_level(uint8_t level)
{
    size_t level_count = sizeof(DELAY_LEVEL_DIVISORS) / sizeof(DELAY_LEVEL_DIVISORS[0]);
    if (level == 0 || level >= level_count) {
        return 0.0f;
    }

    return (60000.0f / (float)s_app.bpm) / DELAY_LEVEL_DIVISORS[level];
}

static void apply_delay_level(void)
{
    float delay_ms = delay_ms_for_level(s_app.delay_level);
    bool enabled = s_app.delay_level > 0;
    s_app.effects[PLAY_UI_EFFECT_DELAY] = enabled;
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_delay_enabled(enabled, delay_ms));
}

static void step_delay_level(void)
{
    size_t level_count = sizeof(DELAY_LEVEL_DIVISORS) / sizeof(DELAY_LEVEL_DIVISORS[0]);
    s_app.delay_level = (uint8_t)((s_app.delay_level + 1U) % level_count);
    apply_delay_level();

    ESP_LOGI(TAG,
             "delay=%s level=%s time=%.1f ms",
             s_app.effects[PLAY_UI_EFFECT_DELAY] ? "on" : "off",
             DELAY_LEVEL_NAMES[s_app.delay_level],
             delay_ms_for_level(s_app.delay_level));
    set_status(s_app.effects[PLAY_UI_EFFECT_DELAY] ? "DELAY ON" : "DELAY OFF");
    update_ui();
}

static void step_tremolo_level(void)
{
    size_t level_count = sizeof(TREMOLO_LEVEL_DIVISORS) / sizeof(TREMOLO_LEVEL_DIVISORS[0]);
    s_app.tremolo_level = (uint8_t)((s_app.tremolo_level + 1U) % level_count);
    s_app.effects[PLAY_UI_EFFECT_TREMOLO] = s_app.tremolo_level > 0;
    s_app.tremolo_phase = 0.0f;
    s_app.tremolo_last_tick = xTaskGetTickCount();

    esp_err_t err = synth_amy_set_tremolo_enabled(s_app.effects[PLAY_UI_EFFECT_TREMOLO]);
    if (err == ESP_OK) {
        err = synth_amy_set_tremolo_amount(0.0f);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply tremolo: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG,
             "tremolo=%s level=%s rate=%.2f Hz",
             s_app.effects[PLAY_UI_EFFECT_TREMOLO] ? "on" : "off",
             TREMOLO_LEVEL_NAMES[s_app.tremolo_level],
             tremolo_rate_hz_for_level(s_app.tremolo_level));
    set_status(s_app.effects[PLAY_UI_EFFECT_TREMOLO] ? "TREM ON" : "TREM OFF");
    update_ui();
}

static void toggle_current_effect(void)
{
    play_ui_effect_t effect = (play_ui_effect_t)(s_app.effect_index % PLAY_UI_EFFECT_COUNT);
    if (effect == PLAY_UI_EFFECT_JOYSTICK) {
        step_joystick_mode();
        return;
    }
    if (effect == PLAY_UI_EFFECT_LFO) {
        step_lfo_level();
        return;
    }
    if (effect == PLAY_UI_EFFECT_DELAY) {
        step_delay_level();
        return;
    }
    if (effect == PLAY_UI_EFFECT_TREMOLO) {
        step_tremolo_level();
        return;
    }

    s_app.effects[effect] = !s_app.effects[effect];
    esp_err_t err = ESP_OK;
    switch (effect) {
    case PLAY_UI_EFFECT_REVERB:
        err = synth_amy_set_reverb_enabled(s_app.effects[effect]);
        break;
    case PLAY_UI_EFFECT_CHORUS:
        err = synth_amy_set_chorus_enabled(s_app.effects[effect]);
        break;
    case PLAY_UI_EFFECT_FILTER:
        err = synth_amy_set_filter_enabled(s_app.effects[effect]);
        break;
    case PLAY_UI_EFFECT_FLANGER:
        err = synth_amy_set_flanger_enabled(s_app.effects[effect]);
        break;
    default:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply %s: %s", play_ui_effect_name(effect), esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "%s=%s", play_ui_effect_name(effect), s_app.effects[effect] ? "on" : "off");
    set_status(s_app.effects[effect] ? "FX ON" : "FX OFF");
    update_ui();
}

static void update_sound_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_sound(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_sound(1);
        break;
    case CONTROL_JOYSTICK_UP:
        step_effect(-1);
        break;
    case CONTROL_JOYSTICK_DOWN:
        step_effect(1);
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void step_bpm(int16_t delta)
{
    int next = (int)s_app.bpm + delta;
    if (next < BPM_MIN) {
        next = BPM_MIN;
    } else if (next > BPM_MAX) {
        next = BPM_MAX;
    }

    if (next == s_app.bpm) {
        return;
    }

    s_app.bpm = (uint16_t)next;
    ESP_LOGI(TAG, "bpm=%u", s_app.bpm);
    if (s_app.effects[PLAY_UI_EFFECT_DELAY]) {
        apply_delay_level();
    }
    set_status("BPM CHANGED");
    update_ui();
}

static float wheel_to_filter_cutoff_hz(uint8_t percent)
{
    float t = (float)percent / 100.0f;
    return 20.0f * powf(1000.0f, t);
}

static bool wheel_percent_changed(uint8_t percent)
{
    if (s_app.last_applied_wheel_percent == UINT8_MAX) {
        return true;
    }

    uint8_t last = s_app.last_applied_wheel_percent;
    uint8_t delta = percent > last ? (uint8_t)(percent - last) : (uint8_t)(last - percent);
    return delta >= WHEEL_CHANGE_THRESHOLD_PERCENT;
}

static bool apply_wheel(const control_wheel_state_t *wheel)
{
    if (wheel == NULL || !wheel->present || !wheel_percent_changed(wheel->percent)) {
        return false;
    }

    s_app.last_applied_wheel_percent = wheel->percent;
    if (controls_switch_is_pressed(CONTROL_MODE_MENU) ||
        (controls_switch_is_pressed(CONTROL_SOUND_MENU) &&
         s_app.effect_index == PLAY_UI_EFFECT_FILTER)) {
        float cutoff_hz = wheel_to_filter_cutoff_hz(wheel->percent);
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_filter_cutoff(cutoff_hz));
        set_status("FILTER");
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_volume(wheel->value));
        set_status("VOLUME");
    }
    return true;
}

static void update_wheel(void)
{
    control_wheel_state_t wheel = {0};
    if (controls_wheel_read(&wheel) != ESP_OK) {
        return;
    }

    bool display_changed = s_app.wheel_present != wheel.present ||
        s_app.wheel_percent != wheel.percent;
    s_app.wheel_present = wheel.present;
    s_app.wheel_percent = wheel.percent;
    bool applied = apply_wheel(&wheel);
    if (display_changed || applied) {
        update_ui();
    }
}

static void step_mode(int8_t steps)
{
    synth_play_mode_t previous = s_app.mode;
    synth_play_mode_t next = synth_play_mode_step(s_app.mode, steps);
    if (previous == next) {
        return;
    }

    if (previous == SYNTH_PLAY_MODE_ARP) {
        stop_arp();
        clear_arp_sources();
    }

    s_app.mode = next;
    if (s_app.mode == SYNTH_PLAY_MODE_ARP) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        clear_arp_sources();
        refresh_held_chords();
    } else if (previous == SYNTH_PLAY_MODE_ARP && s_app.held_mask != 0) {
        refresh_held_chords();
    }

    ESP_LOGI(TAG, "mode=%s", synth_play_mode_name(s_app.mode));
    set_status("MODE CHANGED");
    update_ui();
}

static void update_mode_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_bpm(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_bpm(1);
        break;
    case CONTROL_JOYSTICK_UP:
        step_mode(-1);
        break;
    case CONTROL_JOYSTICK_DOWN:
        step_mode(1);
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void update_joystick(void)
{
    control_joystick_state_t joystick = {0};
    if (controls_joystick_read(&joystick) != ESP_OK) {
        return;
    }
    s_app.joystick_direction = joystick.direction;

    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        update_key_menu(joystick.direction);
        update_ui();
        return;
    }
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        update_sound_menu(joystick.direction);
        update_ui();
        return;
    }
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        update_mode_menu(joystick.direction);
        update_ui();
        return;
    }
    s_app.last_menu_direction = CONTROL_JOYSTICK_CENTER;

    if (s_app.joystick_mode == SYNTH_JOYSTICK_MODE_CHROMATIC &&
        s_app.held_mask == 0 &&
        (joystick.direction == CONTROL_JOYSTICK_LEFT ||
         joystick.direction == CONTROL_JOYSTICK_RIGHT)) {
        if (joystick.direction != s_app.last_play_direction) {
            step_key(joystick.direction == CONTROL_JOYSTICK_RIGHT ? 1 : -1);
        }
        s_app.last_play_direction = joystick.direction;
        if (s_app.modifier != SYNTH_CHORD_MOD_NONE) {
            s_app.modifier = SYNTH_CHORD_MOD_NONE;
            update_ui();
        }
        return;
    }

    s_app.last_play_direction = joystick.direction;
    synth_chord_modifier_t modifier = synth_chord_modifier_from_joystick(s_app.joystick_mode, joystick.direction);
    if (modifier == s_app.modifier) {
        return;
    }

    s_app.modifier = modifier;
    ESP_LOGI(TAG, "joystick modifier=%s", synth_chord_modifier_name(modifier));
    refresh_held_chords();
    update_ui();
}

static void handle_menu_event(const control_event_t *event)
{
    if (event->type != CONTROL_EVENT_SWITCH_PRESSED) {
        update_ui();
        return;
    }

    if (event->sw == CONTROL_JOYSTICK_CLICK) {
        if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
            toggle_current_effect();
        } else if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
            set_status("LOOPER WAITING");
            update_ui();
        } else if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
            set_status("KEY MENU");
            update_ui();
        }
    } else if (event->sw == CONTROL_KEY_MENU ||
               event->sw == CONTROL_SOUND_MENU ||
               event->sw == CONTROL_MODE_MENU) {
        update_ui();
    }
}

static void app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "chord synth app started key=%s scale=%s",
             synth_key_name(s_app.key),
             synth_scale_name(s_app.scale));

    while (true) {
        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            handle_menu_event(&event);
            handle_chord_event(&event);
        }

        update_joystick();
        update_arp();
        update_lfo();
        update_tremolo();
        update_wheel();
        vTaskDelay(APP_UPDATE_TICKS);
    }
}

esp_err_t synth_app_start(const esp_pocket_audio_hw_t *hw)
{
    if (s_app.task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(synth_amy_init(hw), TAG, "AMY synth init failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_sound(s_app.sound), TAG, "AMY sound setup failed");
    ESP_RETURN_ON_ERROR(play_ui_start(hw), TAG, "play UI start failed");
    update_ui();

    BaseType_t task_created = xTaskCreatePinnedToCore(
        app_task,
        "synth_app",
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        &s_app.task,
        APP_TASK_CORE
    );

    return task_created == pdPASS ? ESP_OK : ESP_FAIL;
}
