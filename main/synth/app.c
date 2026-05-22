#include "synth/app.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "hmi/play_ui.h"
#include "synth/amy_engine.h"
#include "synth/chord_engine.h"
#include "synth/looper.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

typedef struct {
    synth_key_t key;
    synth_scale_t scale;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_joystick_mode_t joystick_mode;
    synth_bass_mode_t bass_mode;
    bool voice_leading_enabled;
    synth_chord_modifier_t locked_modifier[SYNTH_CHORD_BUTTON_COUNT];
    int8_t chord_inversion[SYNTH_CHORD_BUTTON_COUNT];
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
    uint8_t volume_percent;
    uint8_t last_applied_wheel_percent;
    control_joystick_direction_t last_menu_direction;
    control_joystick_direction_t last_play_direction;
    control_joystick_direction_t last_looper_direction;
    control_joystick_direction_t joystick_direction;
    uint8_t held_mask;
    uint8_t focus_slot;
    uint8_t slash_bass_slot;
    bool looper_visible;
    bool looper_menu_held;
    bool looper_menu_clear_used;
    TickType_t looper_next_ui_tick;
    bool mode_bpm_submenu;
    synth_strum_speed_t strum_speed;
    synth_arp_pattern_t arp_pattern;
    synth_rhythm_rate_t arp_rate;
    synth_arp_chord_mode_t arp_chord_mode;
    synth_rhythm_rate_t repeat_rate;
    synth_chord_t held_chords[SYNTH_CHORD_BUTTON_COUNT];
    bool held_chord_valid[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_t strum_chord;
    bool strum_active;
    uint8_t strum_slot;
    uint8_t strum_voice_index;
    TickType_t strum_next_tick;
    bool drone_active;
    uint8_t drone_slot;
    synth_chord_t arp_chords[SYNTH_CHORD_BUTTON_COUNT];
    bool arp_chord_valid[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_t voice_anchor;
    bool voice_anchor_valid;
    uint32_t arp_step_index;
    uint8_t arp_last_slot;
    TickType_t arp_next_tick;
    bool repeat_active;
    bool repeat_gate_open;
    uint8_t repeat_slot;
    TickType_t repeat_next_tick;
    uint8_t mode_tap_count;
    TickType_t mode_tap_first_tick;
    TickType_t mode_tap_last_tick;
    char chord_name[16];
    char status[22];
    TaskHandle_t task;
} synth_app_state_t;

static const char *TAG = "SYNTH_APP";

static const uint32_t APP_TASK_STACK_SIZE = 4096;
static const UBaseType_t APP_TASK_PRIORITY = 5;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t APP_UPDATE_TICKS = pdMS_TO_TICKS(20);
static const int8_t GLOBAL_OCTAVE_MIN = -1;
static const int8_t GLOBAL_OCTAVE_MAX = 2;
static const int8_t BUTTON_OCTAVE_MIN = -2;
static const int8_t BUTTON_OCTAVE_MAX = 1;
static const int8_t CHORD_INVERSION_MIN = 0;
static const int8_t CHORD_INVERSION_MAX = SYNTH_CHORD_INVERSION_COUNT - 1;
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
static const TickType_t MODE_TAP_RESET_TICKS = pdMS_TO_TICKS(2000);
static const uint8_t ARP_MAX_STEP_NOTES = 2;

static synth_app_state_t s_app = {
    .key = SYNTH_KEY_C,
    .scale = SYNTH_SCALE_MAJOR,
    .global_octave_shift = 0,
    .modifier = SYNTH_CHORD_MOD_NONE,
    .joystick_mode = SYNTH_JOYSTICK_MODE_DEFAULT,
    .bass_mode = SYNTH_BASS_MODE_OFF,
    .voice_leading_enabled = false,
    .sound = SYNTH_SOUND_SAW,
    .mode = SYNTH_PLAY_MODE_PLAY,
    .bpm = 120,
    .effect_index = PLAY_UI_EFFECT_JOYSTICK,
    .effects = {
        [PLAY_UI_EFFECT_FILTER] = true,
        [PLAY_UI_EFFECT_STEREO] = ESP_POCKET_AUDIO_AMY_STEREO_PAIRS != 0,
    },
    .strum_speed = SYNTH_STRUM_SPEED_MEDIUM,
    .arp_pattern = SYNTH_ARP_PATTERN_UP,
    .arp_rate = SYNTH_RHYTHM_RATE_EIGHTH,
    .arp_chord_mode = SYNTH_ARP_CHORD_MODE_ARP_ONLY,
    .repeat_rate = SYNTH_RHYTHM_RATE_EIGHTH,
    .wheel_percent = 100,
    .volume_percent = 100,
    .last_applied_wheel_percent = UINT8_MAX,
    .last_menu_direction = CONTROL_JOYSTICK_CENTER,
    .last_play_direction = CONTROL_JOYSTICK_CENTER,
    .last_looper_direction = CONTROL_JOYSTICK_CENTER,
    .joystick_direction = CONTROL_JOYSTICK_CENTER,
    .focus_slot = 0,
    .slash_bass_slot = UINT8_MAX,
    .strum_slot = UINT8_MAX,
    .drone_slot = UINT8_MAX,
    .arp_last_slot = UINT8_MAX,
    .repeat_slot = UINT8_MAX,
    .chord_name = "---",
    .status = "READY",
};

static void step_mode(int8_t steps);

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
    if (s_app.looper_visible) {
        return PLAY_UI_SCREEN_LOOPER;
    }
    return PLAY_UI_SCREEN_HOME;
}

static void set_status(const char *status)
{
    if (status != NULL) {
        strlcpy(s_app.status, status, sizeof(s_app.status));
    }
}

static bool any_chord_locked(void)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if (s_app.locked_modifier[slot] != SYNTH_CHORD_MOD_NONE) {
            return true;
        }
    }
    return false;
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
    model.bass_mode = s_app.bass_mode;
    model.voice_leading_enabled = s_app.voice_leading_enabled;
    model.chord_lock_enabled = any_chord_locked();
    model.sound = s_app.sound;
    model.mode = s_app.mode;
    model.bpm = s_app.bpm;
    model.mode_bpm_submenu = s_app.mode_bpm_submenu;
    model.strum_speed = s_app.strum_speed;
    model.arp_pattern = s_app.arp_pattern;
    model.arp_rate = s_app.arp_rate;
    model.arp_chord_mode = s_app.arp_chord_mode;
    model.repeat_rate = s_app.repeat_rate;
    model.held_mask = s_app.held_mask;
    model.focus_slot = s_app.focus_slot;
    model.effect_index = s_app.effect_index;
    memcpy(model.effects, s_app.effects, sizeof(model.effects));
    model.lfo_level = s_app.lfo_level;
    model.delay_level = s_app.delay_level;
    model.tremolo_level = s_app.tremolo_level;
    model.wheel_present = s_app.wheel_present;
    model.wheel_percent = s_app.wheel_percent;
    model.volume_percent = s_app.volume_percent;
    model.joystick_direction = s_app.joystick_direction;
    synth_looper_get_snapshot(&model.looper);
    strlcpy(model.chord_name, s_app.chord_name, sizeof(model.chord_name));
    strlcpy(model.status, s_app.status, sizeof(model.status));
    play_ui_update(&model);
}

static uint8_t slot_from_degree(uint8_t degree)
{
    return degree > 0 ? (uint8_t)(degree - 1) : 0;
}

static synth_chord_modifier_t effective_modifier_for_slot(uint8_t slot)
{
    if (s_app.modifier != SYNTH_CHORD_MOD_NONE &&
        slot == s_app.focus_slot &&
        (s_app.held_mask & (1U << slot)) != 0) {
        return s_app.modifier;
    }
    if (slot < SYNTH_CHORD_BUTTON_COUNT) {
        return s_app.locked_modifier[slot];
    }
    return SYNTH_CHORD_MOD_NONE;
}

static uint8_t first_held_slot_except(uint8_t excluded_slot)
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if (slot != excluded_slot && (s_app.held_mask & (1U << slot)) != 0) {
            return slot;
        }
    }
    return UINT8_MAX;
}

static uint8_t bass_slot_for_chord(uint8_t chord_slot)
{
    if (s_app.bass_mode != SYNTH_BASS_MODE_SLASH) {
        return chord_slot;
    }
    if (s_app.slash_bass_slot < SYNTH_CHORD_BUTTON_COUNT &&
        s_app.slash_bass_slot != chord_slot &&
        (s_app.held_mask & (1U << s_app.slash_bass_slot)) != 0) {
        return s_app.slash_bass_slot;
    }

    uint8_t held_slot = first_held_slot_except(chord_slot);
    return held_slot < SYNTH_CHORD_BUTTON_COUNT ? held_slot : chord_slot;
}

static bool build_chord(uint8_t degree, synth_chord_t *chord)
{
    uint8_t slot = slot_from_degree(degree);
    uint8_t bass_slot = bass_slot_for_chord(slot);
    synth_chord_request_t request = {
        .key = s_app.key,
        .scale = s_app.scale,
        .degree = degree,
        .global_octave_shift = s_app.global_octave_shift,
        .button_octave_shift = s_app.button_octave_shift[slot],
        .modifier = effective_modifier_for_slot(slot),
        .bass_mode = s_app.bass_mode,
        .inversion = s_app.chord_inversion[slot],
        .bass_degree = (uint8_t)(bass_slot + 1),
        .bass_octave_shift = s_app.button_octave_shift[bass_slot],
        .voice_leading_enabled = s_app.voice_leading_enabled,
    };
    if (s_app.voice_leading_enabled && s_app.voice_anchor_valid) {
        request.previous_note_count = s_app.voice_anchor.note_count;
        if (request.previous_note_count > SYNTH_MAX_CHORD_NOTES) {
            request.previous_note_count = SYNTH_MAX_CHORD_NOTES;
        }
        memcpy(request.previous_notes, s_app.voice_anchor.notes, request.previous_note_count);
    }
    return synth_build_chord(&request, chord) == 0;
}

static void remember_voice_anchor(const synth_chord_t *chord)
{
    if (chord == NULL || chord->note_count == 0) {
        return;
    }
    s_app.voice_anchor = *chord;
    s_app.voice_anchor_valid = true;
}

static void clear_voice_anchor(void)
{
    memset(&s_app.voice_anchor, 0, sizeof(s_app.voice_anchor));
    s_app.voice_anchor_valid = false;
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

static uint32_t rhythm_rate_interval_ms(synth_rhythm_rate_t rate, uint32_t step_index)
{
    float beat_ms = 60000.0f / (float)s_app.bpm;
    float interval_ms = beat_ms * 0.5f;

    switch (rate) {
    case SYNTH_RHYTHM_RATE_WHOLE:
        interval_ms = beat_ms * 4.0f;
        break;
    case SYNTH_RHYTHM_RATE_HALF:
        interval_ms = beat_ms * 2.0f;
        break;
    case SYNTH_RHYTHM_RATE_QUARTER:
        interval_ms = beat_ms;
        break;
    case SYNTH_RHYTHM_RATE_EIGHTH:
        interval_ms = beat_ms * 0.5f;
        break;
    case SYNTH_RHYTHM_RATE_SIXTEENTH:
        interval_ms = beat_ms * 0.25f;
        break;
    case SYNTH_RHYTHM_RATE_SIXTEENTH_TRIPLET:
        interval_ms = beat_ms / 6.0f;
        break;
    case SYNTH_RHYTHM_RATE_THIRTY_SECOND:
        interval_ms = beat_ms * 0.125f;
        break;
    case SYNTH_RHYTHM_RATE_SWING_EIGHTH:
        interval_ms = (step_index & 1U) == 0 ? beat_ms * (2.0f / 3.0f) : beat_ms * (1.0f / 3.0f);
        break;
    case SYNTH_RHYTHM_RATE_SWING_SIXTEENTH:
        interval_ms = (step_index & 1U) == 0 ? beat_ms * (1.0f / 3.0f) : beat_ms * (1.0f / 6.0f);
        break;
    default:
        break;
    }

    if (interval_ms < 20.0f) {
        interval_ms = 20.0f;
    }
    return (uint32_t)(interval_ms + 0.5f);
}

static TickType_t rhythm_rate_ticks(synth_rhythm_rate_t rate, uint32_t step_index)
{
    return pdMS_TO_TICKS(rhythm_rate_interval_ms(rate, step_index));
}

static void sort_notes(uint8_t *notes, uint8_t note_count)
{
    for (uint8_t i = 1; i < note_count; ++i) {
        uint8_t note = notes[i];
        uint8_t j = i;
        while (j > 0 && notes[j - 1] > note) {
            notes[j] = notes[j - 1];
            --j;
        }
        notes[j] = note;
    }
}

static uint8_t first_valid_held_slot(const bool valid[SYNTH_CHORD_BUTTON_COUNT])
{
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) != 0 && valid[slot]) {
            return slot;
        }
    }
    return ARP_NO_SLOT;
}

static uint8_t active_valid_held_slot(const bool valid[SYNTH_CHORD_BUTTON_COUNT])
{
    if ((s_app.held_mask & (1U << s_app.focus_slot)) != 0 && valid[s_app.focus_slot]) {
        return s_app.focus_slot;
    }
    return first_valid_held_slot(valid);
}

static void stop_strum(void)
{
    if (s_app.strum_slot != ARP_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.strum_slot));
    }
    s_app.strum_active = false;
    s_app.strum_slot = ARP_NO_SLOT;
    s_app.strum_voice_index = 0;
}

static void start_strum(uint8_t slot, const synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
    s_app.strum_chord = *chord;
    s_app.strum_slot = slot;
    s_app.strum_voice_index = 0;
    s_app.strum_next_tick = xTaskGetTickCount();
    s_app.strum_active = true;
    set_status("STRUM");
}

static void update_strum(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_STRUM || !s_app.strum_active) {
        return;
    }
    if ((s_app.held_mask & (1U << s_app.strum_slot)) == 0) {
        stop_strum();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.strum_next_tick) < 0) {
        return;
    }

    esp_err_t err = synth_amy_chord_voice_on(s_app.strum_slot, &s_app.strum_chord, s_app.strum_voice_index);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NOT_SUPPORTED) {
        s_app.strum_active = false;
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to strum voice %u: %s",
                 s_app.strum_voice_index,
                 esp_err_to_name(err));
        s_app.strum_active = false;
        return;
    }

    ++s_app.strum_voice_index;
    s_app.strum_next_tick = now + pdMS_TO_TICKS(synth_strum_speed_delay_ms(s_app.strum_speed));
}

static uint8_t active_arp_slot(void)
{
    return active_valid_held_slot(s_app.arp_chord_valid);
}

static void stop_arp(void)
{
    if (s_app.arp_last_slot != ARP_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.arp_last_slot));
    }
    s_app.arp_last_slot = ARP_NO_SLOT;
    s_app.arp_step_index = 0;
}

static void clear_arp_sources(void)
{
    memset(s_app.arp_chord_valid, 0, sizeof(s_app.arp_chord_valid));
    s_app.arp_step_index = 0;
    s_app.arp_next_tick = xTaskGetTickCount();
}

static uint8_t arp_pattern_note_index(uint8_t note_count, uint32_t step_index)
{
    if (note_count <= 1) {
        return 0;
    }

    switch (s_app.arp_pattern) {
    case SYNTH_ARP_PATTERN_DOWN:
        return (uint8_t)((note_count - 1U) - (step_index % note_count));
    case SYNTH_ARP_PATTERN_UP_DOWN: {
        uint8_t cycle = (uint8_t)((note_count * 2U) - 2U);
        uint8_t pos = (uint8_t)(step_index % cycle);
        return pos < note_count ? pos : (uint8_t)(cycle - pos);
    }
    case SYNTH_ARP_PATTERN_DOWN_UP: {
        uint8_t cycle = (uint8_t)((note_count * 2U) - 2U);
        uint8_t pos = (uint8_t)(step_index % cycle);
        uint8_t up_index = pos < note_count ? pos : (uint8_t)(cycle - pos);
        return (uint8_t)((note_count - 1U) - up_index);
    }
    case SYNTH_ARP_PATTERN_RANDOM:
        return (uint8_t)(esp_random() % note_count);
    case SYNTH_ARP_PATTERN_UP:
    case SYNTH_ARP_PATTERN_FINGERPICK:
    default:
        return (uint8_t)(step_index % note_count);
    }
}

static void build_fingerpick_step(const uint8_t *notes,
                                  uint8_t note_count,
                                  uint32_t step_index,
                                  synth_chord_t *step)
{
    if (note_count == 0 || step == NULL) {
        return;
    }

    uint8_t high = note_count > 2 ? 2 : (uint8_t)(note_count - 1);
    uint8_t alt = note_count > 3 ? 3 : high;
    uint8_t low = (step_index & 1U) != 0 && note_count > 1 ? 1 : 0;
    uint8_t top = (step_index & 2U) != 0 ? alt : high;

    step->notes[0] = notes[low];
    step->note_count = 1;
    if (top != low && step->note_count < ARP_MAX_STEP_NOTES) {
        step->notes[step->note_count++] = notes[top];
    }
}

static void append_step_notes(synth_chord_t *dest, const synth_chord_t *step)
{
    if (dest == NULL || step == NULL) {
        return;
    }

    for (uint8_t i = 0; i < step->note_count && dest->note_count < SYNTH_MAX_CHORD_NOTES; ++i) {
        bool duplicate = false;
        for (uint8_t j = 0; j < dest->note_count; ++j) {
            if (dest->notes[j] == step->notes[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            dest->notes[dest->note_count++] = step->notes[i];
        }
    }
}

static void build_arp_step_chord(const synth_chord_t *source, synth_chord_t *step)
{
    if (source == NULL || step == NULL || source->note_count == 0) {
        return;
    }

    uint8_t notes[SYNTH_MAX_CHORD_NOTES] = {0};
    uint8_t note_count = source->note_count;
    if (note_count > SYNTH_MAX_CHORD_NOTES) {
        note_count = SYNTH_MAX_CHORD_NOTES;
    }
    memcpy(notes, source->notes, note_count);
    sort_notes(notes, note_count);

    *step = *source;
    strlcpy(step->name, "ARP", sizeof(step->name));
    step->note_count = 0;

    if (s_app.arp_pattern == SYNTH_ARP_PATTERN_FINGERPICK) {
        build_fingerpick_step(notes, note_count, s_app.arp_step_index, step);
        return;
    }

    step->notes[0] = notes[arp_pattern_note_index(note_count, s_app.arp_step_index)];
    step->note_count = 1;
}

static void update_arp(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_ARPEGGIO) {
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

    synth_chord_t step = {0};
    build_arp_step_chord(source, &step);

    synth_chord_t output = step;
    if (s_app.arp_chord_mode == SYNTH_ARP_CHORD_MODE_CHORD_ARP) {
        output = *source;
        append_step_notes(&output, &step);
    } else if (s_app.arp_chord_mode == SYNTH_ARP_CHORD_MODE_RHYTHM_ARP) {
        output = (s_app.arp_step_index & 1U) == 0 ? *source : step;
        if ((s_app.arp_step_index & 1U) == 0) {
            append_step_notes(&output, &step);
        }
    }

    esp_err_t err = synth_amy_chord_on(slot, &output);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play arp step: %s", esp_err_to_name(err));
        return;
    }
    remember_voice_anchor(&output);

    char note_name[48];
    format_chord_notes(&step, note_name, sizeof(note_name));
    ESP_LOGD(TAG,
             "arp %s slot=%u pattern=%s rate=%s mode=%s step=%lu notes=%s",
             source->name,
             (unsigned)(slot + 1),
             synth_arp_pattern_name(s_app.arp_pattern),
             synth_rhythm_rate_name(s_app.arp_rate),
             synth_arp_chord_mode_name(s_app.arp_chord_mode),
             (unsigned long)s_app.arp_step_index,
             note_name);

    s_app.arp_last_slot = slot;
    s_app.arp_next_tick = now + rhythm_rate_ticks(s_app.arp_rate, s_app.arp_step_index);
    ++s_app.arp_step_index;
}

static void stop_repeat(void)
{
    if (s_app.repeat_active && s_app.repeat_slot != ARP_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.repeat_slot));
    }
    s_app.repeat_active = false;
    s_app.repeat_gate_open = false;
    s_app.repeat_slot = ARP_NO_SLOT;
}

static void update_repeat(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_REPEAT) {
        return;
    }

    uint8_t slot = active_valid_held_slot(s_app.held_chord_valid);
    if (slot == ARP_NO_SLOT) {
        stop_repeat();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.repeat_next_tick) < 0) {
        return;
    }

    uint32_t interval_ms = rhythm_rate_interval_ms(s_app.repeat_rate, 0);
    TickType_t gate_ticks = pdMS_TO_TICKS(interval_ms / 2U);
    if (gate_ticks < pdMS_TO_TICKS(20)) {
        gate_ticks = pdMS_TO_TICKS(20);
    }

    if (s_app.repeat_gate_open) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.repeat_slot));
        s_app.repeat_gate_open = false;
        s_app.repeat_next_tick = now + gate_ticks;
        return;
    }

    s_app.repeat_slot = slot;
    esp_err_t err = synth_amy_chord_on(slot, &s_app.held_chords[slot]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to repeat chord: %s", esp_err_to_name(err));
        return;
    }
    remember_voice_anchor(&s_app.held_chords[slot]);
    s_app.repeat_active = true;
    s_app.repeat_gate_open = true;
    s_app.repeat_next_tick = now + gate_ticks;
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

static void warn_unimplemented_mode(synth_play_mode_t mode)
{
    ESP_LOGW(TAG, "play mode %s is visible in UI but not implemented yet", synth_play_mode_name(mode));
    set_status("WARN MODE TODO");
}

static void log_chord_event(const char *verb, const synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    char note_names[96];
    format_chord_notes(chord, note_names, sizeof(note_names));
    ESP_LOGI(TAG,
             "%s %s mode=%s key=%s octave=%d mod=%s notes=%u [%s]",
             verb,
             chord->name,
             synth_play_mode_name(s_app.mode),
             synth_key_name(s_app.key),
             s_app.global_octave_shift,
             synth_chord_modifier_name(chord->modifier),
             chord->note_count,
             note_names);
}

static void make_root_note_chord(const synth_chord_t *source, synth_chord_t *note)
{
    if (source == NULL || note == NULL) {
        return;
    }

    *note = *source;
    note->notes[0] = source->root_midi;
    note->note_count = 1;
    note->bass_enabled = false;
    note->bass_mode = SYNTH_BASS_MODE_OFF;
    snprintf(note->name, sizeof(note->name), "%s", synth_key_name((synth_key_t)(source->root_midi % 12)));
}

static void append_unique_layer_note(synth_chord_t *layer, uint8_t note)
{
    if (layer == NULL || layer->note_count >= SYNTH_MAX_CHORD_NOTES) {
        return;
    }

    for (uint8_t i = 0; i < layer->note_count; ++i) {
        if (layer->notes[i] == note) {
            return;
        }
    }
    layer->notes[layer->note_count++] = note;
}

static uint8_t held_chord_count(void)
{
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) != 0 && s_app.held_chord_valid[slot]) {
            ++count;
        }
    }
    return count;
}

static bool build_play_layer(synth_chord_t *layer)
{
    if (layer == NULL) {
        return false;
    }

    memset(layer, 0, sizeof(*layer));
    bool initialized = false;
    uint8_t chord_count = 0;
    uint8_t held_count = held_chord_count();
    uint8_t slash_bass_slot = bass_slot_for_chord(UINT8_MAX);

    for (uint8_t slot = 0; slot < SYNTH_CHORD_BUTTON_COUNT; ++slot) {
        if ((s_app.held_mask & (1U << slot)) == 0 || !s_app.held_chord_valid[slot]) {
            continue;
        }
        if (s_app.bass_mode == SYNTH_BASS_MODE_SLASH &&
            held_count > 1 &&
            slot == slash_bass_slot) {
            continue;
        }

        const synth_chord_t *chord = &s_app.held_chords[slot];
        if (!initialized) {
            *layer = *chord;
            layer->note_count = 0;
            initialized = true;
        }
        ++chord_count;
        for (uint8_t note_index = 0; note_index < chord->note_count; ++note_index) {
            append_unique_layer_note(layer, chord->notes[note_index]);
        }
    }

    if (!initialized || layer->note_count == 0) {
        return false;
    }
    if (chord_count > 1) {
        strlcpy(layer->name, "LAYER", sizeof(layer->name));
    }
    return true;
}

static void render_play_layer(void)
{
    synth_chord_t layer = {0};
    if (!build_play_layer(&layer)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        strlcpy(s_app.chord_name, "---", sizeof(s_app.chord_name));
        set_status("READY");
        update_ui();
        return;
    }

    esp_err_t err = synth_amy_chord_on(0, &layer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play layer: %s", esp_err_to_name(err));
        return;
    }
    remember_voice_anchor(&layer);

    log_chord_event("play", &layer);
    strlcpy(s_app.chord_name, layer.name, sizeof(s_app.chord_name));
    set_status(layer.note_count > 3 ? "LAYER" : "PLAYING");
    update_ui();
}

static void play_degree(uint8_t degree)
{
    synth_chord_t chord = {0};
    if (!build_chord(degree, &chord)) {
        ESP_LOGW(TAG, "failed to build chord degree=%u", degree);
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    s_app.held_chords[slot] = chord;
    s_app.held_chord_valid[slot] = true;

    if (!synth_play_mode_is_implemented(s_app.mode)) {
        warn_unimplemented_mode(s_app.mode);
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        s_app.arp_chords[slot] = chord;
        s_app.arp_chord_valid[slot] = true;
        s_app.arp_step_index = 0;
        s_app.arp_next_tick = xTaskGetTickCount();
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        set_status("ARP");
        log_chord_event("arp source", &chord);
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_STRUM) {
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        log_chord_event("strum", &chord);
        start_strum(slot, &chord);
        remember_voice_anchor(&chord);
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_LEAD) {
        synth_chord_t note = {0};
        make_root_note_chord(&chord, &note);
        esp_err_t err = synth_amy_chord_on(slot, &note);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to play lead note: %s", esp_err_to_name(err));
            return;
        }
        remember_voice_anchor(&note);
        log_chord_event("lead", &note);
        strlcpy(s_app.chord_name, note.name, sizeof(s_app.chord_name));
        set_status("LEAD");
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_DRONE) {
        if (s_app.drone_active && s_app.drone_slot != slot) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.drone_slot));
        }
        esp_err_t err = synth_amy_chord_on(slot, &chord);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to play drone: %s", esp_err_to_name(err));
            return;
        }
        s_app.drone_active = true;
        s_app.drone_slot = slot;
        remember_voice_anchor(&chord);
        log_chord_event("drone", &chord);
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        set_status("DRONE");
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_REPEAT) {
        s_app.repeat_slot = slot;
        s_app.repeat_active = true;
        s_app.repeat_gate_open = false;
        s_app.repeat_next_tick = xTaskGetTickCount();
        remember_voice_anchor(&chord);
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        set_status("REPEAT");
        log_chord_event("repeat source", &chord);
        update_ui();
        return;
    }

    render_play_layer();
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
    s_app.held_chord_valid[slot] = false;
    s_app.arp_chord_valid[slot] = false;
    if (s_app.slash_bass_slot == slot) {
        uint8_t replacement = first_held_slot_except(UINT8_MAX);
        s_app.slash_bass_slot = replacement < SYNTH_CHORD_BUTTON_COUNT ? replacement : UINT8_MAX;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_DRONE) {
        set_status(s_app.drone_active ? "DRONE" : "READY");
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_PLAY) {
        render_play_layer();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_STRUM && s_app.strum_slot == slot) {
        stop_strum();
    } else if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        if (s_app.arp_last_slot == slot || s_app.held_mask == 0) {
            stop_arp();
        }
    } else if (s_app.mode == SYNTH_PLAY_MODE_REPEAT) {
        if (s_app.repeat_slot == slot || s_app.held_mask == 0) {
            stop_repeat();
        }
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(slot));
    }

    if (s_app.held_mask == 0) {
        stop_arp();
        if (s_app.mode != SYNTH_PLAY_MODE_DRONE) {
            stop_repeat();
        }
        set_status("READY");
    } else if (s_app.mode == SYNTH_PLAY_MODE_PLAY) {
        uint8_t next_slot = active_valid_held_slot(s_app.held_chord_valid);
        if (next_slot != ARP_NO_SLOT) {
            play_degree((uint8_t)(next_slot + 1));
        }
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
        if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
            if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO && degree <= 6) {
                s_app.arp_pattern = (synth_arp_pattern_t)(degree - 1);
                s_app.arp_step_index = 0;
                ESP_LOGI(TAG, "arp pattern=%s", synth_arp_pattern_name(s_app.arp_pattern));
                set_status(synth_arp_pattern_name(s_app.arp_pattern));
            } else if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO && degree == 7) {
                s_app.arp_chord_mode = (synth_arp_chord_mode_t)((s_app.arp_chord_mode + 1U) %
                                                                SYNTH_ARP_CHORD_MODE_COUNT);
                ESP_LOGI(TAG, "arp chord mode=%s", synth_arp_chord_mode_name(s_app.arp_chord_mode));
                set_status(synth_arp_chord_mode_name(s_app.arp_chord_mode));
            } else {
                synth_play_mode_t next = synth_play_mode_quick_select(degree);
                step_mode((int8_t)((int)next - (int)s_app.mode));
                if (synth_play_mode_is_implemented(s_app.mode)) {
                    set_status(synth_play_mode_short_name(s_app.mode));
                }
            }
            update_ui();
            return;
        }

        if (s_app.bass_mode == SYNTH_BASS_MODE_SLASH &&
            s_app.held_mask != 0 &&
            (s_app.held_mask & (1U << slot)) == 0) {
            uint8_t bass_slot = active_valid_held_slot(s_app.held_chord_valid);
            if (bass_slot < SYNTH_CHORD_BUTTON_COUNT) {
                s_app.slash_bass_slot = bass_slot;
            }
        } else if (s_app.bass_mode == SYNTH_BASS_MODE_SLASH &&
                   s_app.held_mask == 0) {
            s_app.slash_bass_slot = slot;
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
    clear_voice_anchor();
    ESP_LOGI(TAG, "key=%s", synth_key_name(s_app.key));
    set_status("KEY CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_scale(int8_t steps)
{
    s_app.scale = synth_scale_step(s_app.scale, steps);
    clear_voice_anchor();
    ESP_LOGI(TAG, "scale=%s", synth_scale_name(s_app.scale));
    set_status(synth_scale_name(s_app.scale));
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
    clear_voice_anchor();
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
    clear_voice_anchor();
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
    s_app.modifier = s_app.held_mask != 0 ?
        synth_chord_modifier_from_joystick(s_app.joystick_mode, s_app.joystick_direction) :
        SYNTH_CHORD_MOD_NONE;
    ESP_LOGI(TAG, "joystick mode=%s", synth_joystick_mode_name(s_app.joystick_mode));
    set_status(synth_joystick_mode_name(s_app.joystick_mode));
    refresh_held_chords();
    update_ui();
}

static void step_bass_mode(void)
{
    s_app.bass_mode = synth_bass_mode_step(s_app.bass_mode, 1);
    if (s_app.bass_mode != SYNTH_BASS_MODE_SLASH) {
        s_app.slash_bass_slot = UINT8_MAX;
    }
    ESP_LOGI(TAG, "bass mode=%s", synth_bass_mode_name(s_app.bass_mode));
    set_status(synth_bass_mode_name(s_app.bass_mode));
    refresh_held_chords();
    update_ui();
}

static void toggle_voice_leading(void)
{
    s_app.voice_leading_enabled = !s_app.voice_leading_enabled;
    if (!s_app.voice_leading_enabled) {
        clear_voice_anchor();
    }
    ESP_LOGI(TAG, "voice leading=%s", s_app.voice_leading_enabled ? "on" : "off");
    set_status(s_app.voice_leading_enabled ? "VOICE LEAD ON" : "VOICE LEAD OFF");
    refresh_held_chords();
    update_ui();
}

static uint8_t focused_held_slot(void)
{
    uint8_t slot = s_app.focus_slot;
    if ((s_app.held_mask & (1U << slot)) == 0) {
        slot = active_valid_held_slot(s_app.held_chord_valid);
        if (slot == ARP_NO_SLOT) {
            return UINT8_MAX;
        }
        s_app.focus_slot = slot;
    }
    return slot;
}

static void cycle_focused_chord_inversion(void)
{
    if (s_app.held_mask == 0) {
        return;
    }

    uint8_t slot = focused_held_slot();
    if (slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return;
    }

    int8_t next = (int8_t)(s_app.chord_inversion[slot] + 1);
    if (next > CHORD_INVERSION_MAX) {
        next = CHORD_INVERSION_MIN;
    }
    s_app.chord_inversion[slot] = next;
    clear_voice_anchor();
    ESP_LOGI(TAG,
             "chord %u inversion=%d",
             (unsigned)(slot + 1),
             s_app.chord_inversion[slot]);
    set_status("INVERSION");
    play_degree((uint8_t)(slot + 1));
    update_ui();
}

static void toggle_focused_chord_lock(void)
{
    if (s_app.held_mask == 0 || s_app.modifier == SYNTH_CHORD_MOD_NONE) {
        return;
    }

    uint8_t slot = focused_held_slot();
    if (slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return;
    }

    if (s_app.locked_modifier[slot] == s_app.modifier) {
        s_app.locked_modifier[slot] = SYNTH_CHORD_MOD_NONE;
        ESP_LOGI(TAG, "chord %u lock cleared", (unsigned)(slot + 1));
        set_status("LOCK OFF");
    } else {
        s_app.locked_modifier[slot] = s_app.modifier;
        ESP_LOGI(TAG,
                 "chord %u lock=%s",
                 (unsigned)(slot + 1),
                 synth_chord_modifier_name(s_app.modifier));
        set_status("LOCKED");
    }
    play_degree((uint8_t)(slot + 1));
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
    if (effect == PLAY_UI_EFFECT_SCALE) {
        step_scale(1);
        return;
    }
    if (effect == PLAY_UI_EFFECT_VOICE_LEADING) {
        toggle_voice_leading();
        return;
    }
    if (effect == PLAY_UI_EFFECT_BASS_MODE) {
        step_bass_mode();
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
    case PLAY_UI_EFFECT_STEREO:
        err = synth_amy_set_stereo_enabled(s_app.effects[effect]);
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

static void cycle_mode_param(void)
{
    switch (s_app.mode) {
    case SYNTH_PLAY_MODE_STRUM:
        s_app.strum_speed = (synth_strum_speed_t)((s_app.strum_speed + 1U) % SYNTH_STRUM_SPEED_COUNT);
        ESP_LOGI(TAG,
                 "strum speed=%s delay=%ums",
                 synth_strum_speed_name(s_app.strum_speed),
                 synth_strum_speed_delay_ms(s_app.strum_speed));
        set_status(synth_strum_speed_name(s_app.strum_speed));
        break;
    case SYNTH_PLAY_MODE_ARPEGGIO:
        s_app.arp_rate = (synth_rhythm_rate_t)((s_app.arp_rate + 1U) % SYNTH_RHYTHM_RATE_COUNT);
        s_app.arp_step_index = 0;
        ESP_LOGI(TAG, "arp rate=%s", synth_rhythm_rate_name(s_app.arp_rate));
        set_status(synth_rhythm_rate_name(s_app.arp_rate));
        break;
    case SYNTH_PLAY_MODE_REPEAT:
        s_app.repeat_rate = (synth_rhythm_rate_t)((s_app.repeat_rate + 1U) % SYNTH_RHYTHM_RATE_COUNT);
        s_app.repeat_next_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "repeat rate=%s", synth_rhythm_rate_name(s_app.repeat_rate));
        set_status(synth_rhythm_rate_name(s_app.repeat_rate));
        break;
    default:
        if (!synth_play_mode_is_implemented(s_app.mode)) {
            warn_unimplemented_mode(s_app.mode);
        } else {
            set_status("NO PARAM");
        }
        break;
    }
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
        s_app.volume_percent = wheel->percent;
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

    stop_strum();
    stop_arp();
    stop_repeat();
    if (previous == SYNTH_PLAY_MODE_DRONE && s_app.drone_active) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.drone_slot));
        s_app.drone_active = false;
        s_app.drone_slot = ARP_NO_SLOT;
    }
    clear_arp_sources();

    s_app.mode = next;
    s_app.mode_bpm_submenu = false;
    if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        clear_arp_sources();
        refresh_held_chords();
    } else if (synth_play_mode_is_implemented(s_app.mode) && s_app.held_mask != 0) {
        refresh_held_chords();
    } else if (!synth_play_mode_is_implemented(s_app.mode)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        warn_unimplemented_mode(s_app.mode);
    }

    ESP_LOGI(TAG, "mode=%s", synth_play_mode_name(s_app.mode));
    if (synth_play_mode_is_implemented(s_app.mode)) {
        set_status("MODE CHANGED");
    }
    update_ui();
}

static void update_mode_menu(control_joystick_direction_t direction)
{
    if (direction == s_app.last_menu_direction) {
        return;
    }
    s_app.last_menu_direction = direction;

    if (s_app.mode_bpm_submenu) {
        switch (direction) {
        case CONTROL_JOYSTICK_LEFT:
            step_bpm(-1);
            break;
        case CONTROL_JOYSTICK_RIGHT:
            step_bpm(1);
            break;
        case CONTROL_JOYSTICK_UP:
        case CONTROL_JOYSTICK_DOWN:
            s_app.mode_bpm_submenu = false;
            set_status("MODE MENU");
            update_ui();
            break;
        case CONTROL_JOYSTICK_CENTER:
        default:
            break;
        }
        return;
    }

    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        step_mode(-1);
        break;
    case CONTROL_JOYSTICK_RIGHT:
        step_mode(1);
        break;
    case CONTROL_JOYSTICK_UP:
        s_app.mode_bpm_submenu = true;
        set_status("BPM");
        update_ui();
        break;
    case CONTROL_JOYSTICK_DOWN:
        cycle_mode_param();
        break;
    case CONTROL_JOYSTICK_CENTER:
    default:
        break;
    }
}

static void open_looper(void)
{
    s_app.looper_visible = true;
    s_app.last_looper_direction = CONTROL_JOYSTICK_CENTER;
    s_app.looper_next_ui_tick = xTaskGetTickCount();
    set_status("LOOPER");
    update_ui();
}

static void handle_looper_click(void)
{
    if (!s_app.looper_visible) {
        open_looper();
        return;
    }

    esp_err_t err = synth_looper_click(s_app.bpm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "looper click failed: %s", esp_err_to_name(err));
        set_status("LOOPER ERR");
    } else {
        set_status("LOOPER");
    }
    update_ui();
}

static void handle_looper_clear_all(void)
{
    esp_err_t err = synth_looper_clear_all();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "looper clear failed: %s", esp_err_to_name(err));
        set_status("LOOPER ERR");
    } else {
        set_status("LOOPER CLR");
    }
    update_ui();
}

static void update_looper_joystick(control_joystick_direction_t direction)
{
    if (direction == s_app.last_looper_direction) {
        return;
    }
    s_app.last_looper_direction = direction;

    esp_err_t err = ESP_OK;
    switch (direction) {
    case CONTROL_JOYSTICK_LEFT:
        if (synth_looper_current_track_is_waiting()) {
            err = synth_looper_adjust_bars(-1);
        } else {
            err = synth_looper_select_track(-1);
        }
        break;
    case CONTROL_JOYSTICK_RIGHT:
        if (synth_looper_current_track_is_waiting()) {
            err = synth_looper_adjust_bars(1);
        } else {
            err = synth_looper_select_track(1);
        }
        break;
    case CONTROL_JOYSTICK_DOWN:
        err = synth_looper_toggle_pause();
        break;
    case CONTROL_JOYSTICK_CENTER:
    case CONTROL_JOYSTICK_UP:
    case CONTROL_JOYSTICK_UP_RIGHT:
    case CONTROL_JOYSTICK_DOWN_RIGHT:
    case CONTROL_JOYSTICK_DOWN_LEFT:
    case CONTROL_JOYSTICK_UP_LEFT:
    default:
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "looper joystick failed: %s", esp_err_to_name(err));
        set_status("LOOPER ERR");
    } else if (direction != CONTROL_JOYSTICK_CENTER) {
        set_status("LOOPER");
    }
    update_ui();
}

static void update_looper_periodic_ui(void)
{
    if (!s_app.looper_visible) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.looper_next_ui_tick) < 0) {
        return;
    }
    s_app.looper_next_ui_tick = now + pdMS_TO_TICKS(250);
    update_ui();
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

    if (s_app.looper_visible && s_app.held_mask == 0) {
        update_looper_joystick(joystick.direction);
        s_app.last_play_direction = CONTROL_JOYSTICK_CENTER;
        return;
    }
    s_app.last_looper_direction = CONTROL_JOYSTICK_CENTER;

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

static void handle_mode_button_tap(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_app.mode_tap_count == 0 ||
        (int32_t)(now - s_app.mode_tap_last_tick) > (int32_t)MODE_TAP_RESET_TICKS) {
        s_app.mode_tap_count = 1;
        s_app.mode_tap_first_tick = now;
        s_app.mode_tap_last_tick = now;
        return;
    }

    ++s_app.mode_tap_count;
    s_app.mode_tap_last_tick = now;
    if (s_app.mode_tap_count < 3) {
        return;
    }

    TickType_t span_ticks = s_app.mode_tap_last_tick - s_app.mode_tap_first_tick;
    uint32_t span_ms = (uint32_t)span_ticks * (uint32_t)portTICK_PERIOD_MS;
    if (span_ms > 0) {
        uint32_t beat_ms = span_ms / (uint32_t)(s_app.mode_tap_count - 1U);
        if (beat_ms > 0) {
            uint16_t bpm = (uint16_t)(60000UL / beat_ms);
            if (bpm < BPM_MIN) {
                bpm = BPM_MIN;
            } else if (bpm > BPM_MAX) {
                bpm = BPM_MAX;
            }
            s_app.bpm = bpm;
            if (s_app.effects[PLAY_UI_EFFECT_DELAY]) {
                apply_delay_level();
            }
            ESP_LOGI(TAG, "tap tempo bpm=%u taps=%u", s_app.bpm, s_app.mode_tap_count);
            set_status("TAP TEMPO");
            update_ui();
        }
    }
}

static void handle_menu_event(const control_event_t *event)
{
    if (event->type != CONTROL_EVENT_SWITCH_PRESSED) {
        if (event->sw == CONTROL_KEY_MENU && s_app.looper_menu_held) {
            if (!s_app.looper_menu_clear_used) {
                s_app.looper_visible = false;
            }
            s_app.looper_menu_held = false;
            s_app.looper_menu_clear_used = false;
        }
        update_ui();
        return;
    }

    if (event->sw == CONTROL_JOYSTICK_CLICK) {
        if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
            toggle_current_effect();
        } else if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
            set_status("MODE MENU");
            update_ui();
        } else if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
            if (s_app.looper_visible) {
                s_app.looper_menu_clear_used = true;
                handle_looper_clear_all();
            } else {
                set_status("KEY MENU");
                update_ui();
            }
        } else {
            handle_looper_click();
        }
    } else if (event->sw == CONTROL_KEY_MENU ||
               event->sw == CONTROL_SOUND_MENU ||
               event->sw == CONTROL_MODE_MENU) {
        if (s_app.held_mask != 0) {
            if (event->sw == CONTROL_KEY_MENU) {
                step_focused_button_octave(-1);
            } else if (event->sw == CONTROL_SOUND_MENU) {
                if (s_app.modifier != SYNTH_CHORD_MOD_NONE) {
                    toggle_focused_chord_lock();
                } else {
                    cycle_focused_chord_inversion();
                }
            } else if (event->sw == CONTROL_MODE_MENU) {
                step_focused_button_octave(1);
            }
            return;
        }

        if (event->sw == CONTROL_KEY_MENU && s_app.looper_visible) {
            s_app.looper_menu_held = true;
            s_app.looper_menu_clear_used = false;
        } else {
            s_app.looper_visible = false;
            s_app.looper_menu_held = false;
            s_app.looper_menu_clear_used = false;
        }
        if (event->sw == CONTROL_MODE_MENU) {
            handle_mode_button_tap();
        }
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
        update_strum();
        update_arp();
        update_repeat();
        update_lfo();
        update_tremolo();
        update_wheel();
        update_looper_periodic_ui();
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
