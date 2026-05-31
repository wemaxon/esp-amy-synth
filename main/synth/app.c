#include "synth/app.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/controls.h"
#include "hmi/keymap.h"
#include "hmi/play_ui.h"
#include "synth/amy_engine.h"
#include "synth/app/chord_tools.h"
#include "synth/app/chords.h"
#include "synth/app/effects.h"
#include "synth/app/key.h"
#include "synth/app/looper.h"
#include "synth/app/mixer.h"
#include "synth/app/modulation.h"
#include "synth/app/play_state.h"
#include "synth/app/rhythm.h"
#include "synth/app/settings.h"
#include "synth/app/sound.h"
#include "synth/app/tempo.h"
#include "synth/app/wheel.h"
#include "synth/chord_engine.h"
#include "synth/looper.h"
#include "synth/play_mode.h"
#include "synth/sound.h"

typedef struct {
    synth_key_t key;
    int8_t global_octave_shift;
    int8_t button_octave_shift[SYNTH_CHORD_BUTTON_COUNT];
    synth_chord_modifier_t modifier;
    synth_joystick_mode_t joystick_mode;
    synth_bass_mode_t bass_mode;
    bool voice_leading_enabled;
    synth_app_chord_state_t chords;
    synth_app_sound_t sound;
    synth_play_mode_t mode;
    synth_app_tempo_t tempo;
    synth_app_effects_t fx;
    synth_app_modulation_t lfo;
    synth_app_modulation_t tremolo;
    synth_app_wheel_t wheel;
    control_joystick_direction_t last_menu_direction;
    control_joystick_direction_t last_play_direction;
    control_joystick_direction_t joystick_direction;
    uint8_t slash_bass_slot;
    synth_app_looper_t looper;
    synth_app_mixer_t mixer;
    bool mode_bpm_submenu;
    synth_app_play_state_t play;
    bool drone_active;
    uint8_t drone_slot;
    char chord_name[16];
    char status[22];
    TaskHandle_t task;
} synth_app_state_t;

static const char *TAG = "SYNTH_APP";

static const uint32_t APP_TASK_STACK_SIZE = 4096;
static const UBaseType_t APP_TASK_PRIORITY = 5;
static const BaseType_t APP_TASK_CORE = 0;
static const TickType_t APP_UPDATE_TICKS = pdMS_TO_TICKS(20);
static const int8_t CHORD_INVERSION_MIN = 0;
static const int8_t CHORD_INVERSION_MAX = SYNTH_CHORD_INVERSION_COUNT - 1;
static const uint8_t WHEEL_CHANGE_THRESHOLD_PERCENT = 2;
static const TickType_t SOUND_BROWSE_APPLY_TICKS = pdMS_TO_TICKS(180);

static synth_app_state_t s_app = {
    .key = SYNTH_KEY_C,
    .global_octave_shift = SYNTH_APP_GLOBAL_OCTAVE_DEFAULT,
    .modifier = SYNTH_CHORD_MOD_NONE,
    .joystick_mode = SYNTH_JOYSTICK_MODE_DEFAULT,
    .bass_mode = SYNTH_BASS_MODE_OFF,
    .voice_leading_enabled = false,
    .chords = SYNTH_APP_CHORD_STATE_INITIALIZER,
    .sound = SYNTH_APP_SOUND_INITIALIZER(SYNTH_SOUND_SAW),
    .mode = SYNTH_PLAY_MODE_PLAY,
    .tempo = SYNTH_APP_TEMPO_INITIALIZER,
    .fx = SYNTH_APP_EFFECTS_INITIALIZER,
    .play = SYNTH_APP_PLAY_STATE_INITIALIZER,
    .wheel = SYNTH_APP_WHEEL_INITIALIZER,
    .last_menu_direction = CONTROL_JOYSTICK_CENTER,
    .last_play_direction = CONTROL_JOYSTICK_CENTER,
    .joystick_direction = CONTROL_JOYSTICK_CENTER,
    .slash_bass_slot = UINT8_MAX,
    .drone_slot = UINT8_MAX,
    .looper = SYNTH_APP_LOOPER_INITIALIZER,
    .mixer = SYNTH_APP_MIXER_INITIALIZER,
    .lfo = SYNTH_APP_MODULATION_INITIALIZER,
    .tremolo = SYNTH_APP_MODULATION_INITIALIZER,
    .chord_name = "---",
    .status = "READY",
};

static void step_mode(int8_t steps);

static uint16_t current_bpm(void)
{
    return synth_app_tempo_bpm(&s_app.tempo);
}
static void apply_pending_sound_now(void);

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
    if (synth_app_looper_visible(&s_app.looper)) {
        return PLAY_UI_SCREEN_LOOPER;
    }
    if (s_app.mode == SYNTH_PLAY_MODE_MIXER) {
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
        if (s_app.chords.locked_modifier[slot] != SYNTH_CHORD_MOD_NONE) {
            return true;
        }
    }
    return false;
}

static synth_amy_envelope_t current_envelope(void)
{
    return synth_app_effects_envelope(&s_app.fx, s_app.mode == SYNTH_PLAY_MODE_REPEAT);
}

static void apply_current_envelope(void)
{
    synth_amy_envelope_t envelope = current_envelope();
    esp_err_t err = synth_amy_set_envelope(&envelope);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply envelope: %s", esp_err_to_name(err));
    }
}

static void apply_current_envelope_if_modified(void)
{
    if (s_app.fx.adsr_modified) {
        apply_current_envelope();
    }
}

static uint16_t current_glide_ms(void)
{
    return synth_app_effects_glide_ms(&s_app.fx);
}

static void apply_current_glide(void)
{
    esp_err_t err = synth_amy_set_portamento(current_glide_ms());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply glide: %s", esp_err_to_name(err));
    }
}

static void update_ui(void)
{
    play_ui_model_t model = {0};
    play_ui_model_init(&model);
    model.screen = active_screen();
    model.key = s_app.key;
    model.global_octave_shift = s_app.global_octave_shift;
    memcpy(model.button_octave_shift, s_app.button_octave_shift, sizeof(model.button_octave_shift));
    model.modifier = s_app.modifier;
    model.joystick_mode = s_app.joystick_mode;
    model.bass_mode = s_app.bass_mode;
    model.voice_leading_enabled = s_app.voice_leading_enabled;
    model.chord_lock_enabled = any_chord_locked();
    model.sound = synth_app_sound_current(&s_app.sound);
    model.mode = s_app.mode;
    model.bpm = current_bpm();
    model.mode_bpm_submenu = s_app.mode_bpm_submenu;
    model.strum_speed = s_app.play.strum_speed;
    model.arp_pattern = s_app.play.arp_pattern;
    model.arp_rate = s_app.play.arp_rate;
    model.arp_chord_mode = s_app.play.arp_chord_mode;
    model.repeat_rate = s_app.play.repeat_rate;
    model.held_mask = s_app.chords.held_mask;
    model.focus_slot = s_app.chords.focus_slot;
    model.effect_index = s_app.fx.effect_index;
    memcpy(model.effects, s_app.fx.enabled, sizeof(model.effects));
    model.lfo_level = s_app.fx.lfo_level;
    model.delay_level = s_app.fx.delay_level;
    model.tremolo_level = s_app.fx.tremolo_level;
    model.voice_level = s_app.fx.voice_level;
    model.adsr_preset = s_app.fx.adsr_preset;
    model.attack_ms = s_app.fx.attack_ms;
    model.release_ms = s_app.fx.release_ms;
    model.glide_level = s_app.fx.glide_level;
    model.wheel_present = synth_app_wheel_present(&s_app.wheel);
    model.wheel_percent = synth_app_wheel_percent(&s_app.wheel);
    model.volume_percent = synth_app_wheel_volume_percent(&s_app.wheel);
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

static bool build_chord(uint8_t degree, synth_chord_t *chord)
{
    synth_app_chord_build_context_t context = {
        .key = s_app.key,
        .global_octave_shift = s_app.global_octave_shift,
        .button_octave_shift = s_app.button_octave_shift,
        .modifier = s_app.modifier,
        .focus_slot = s_app.chords.focus_slot,
        .held_mask = s_app.chords.held_mask,
        .locked_modifier = s_app.chords.locked_modifier,
        .bass_mode = s_app.bass_mode,
        .slash_bass_slot = s_app.slash_bass_slot,
        .chord_inversion = s_app.chords.chord_inversion,
        .voice_leading_enabled = s_app.voice_leading_enabled,
        .voice_anchor = &s_app.chords.voice_anchor,
        .voice_anchor_valid = s_app.chords.voice_anchor_valid,
    };
    return synth_app_build_chord_for_degree(&context, degree, chord);
}

static void stop_strum(void)
{
    if (s_app.play.strum_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.play.strum_slot));
    }
    synth_app_play_stop_strum(&s_app.play);
}

static void start_strum(uint8_t slot, const synth_chord_t *chord)
{
    if (chord == NULL) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
    synth_app_play_start_strum(&s_app.play, slot, chord, xTaskGetTickCount());
    set_status("STRUM");
}

static void update_strum(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_STRUM || !s_app.play.strum_active) {
        return;
    }
    if ((s_app.chords.held_mask & (1U << s_app.play.strum_slot)) == 0) {
        stop_strum();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.play.strum_next_tick) < 0) {
        return;
    }

    esp_err_t err = synth_amy_chord_voice_on(s_app.play.strum_slot, &s_app.play.strum_chord, s_app.play.strum_voice_index);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NOT_SUPPORTED) {
        s_app.play.strum_active = false;
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to strum voice %u: %s",
                 s_app.play.strum_voice_index,
                 esp_err_to_name(err));
        s_app.play.strum_active = false;
        return;
    }

    ++s_app.play.strum_voice_index;
    s_app.play.strum_next_tick = now + pdMS_TO_TICKS(synth_strum_speed_delay_ms(s_app.play.strum_speed));
}

static uint8_t active_arp_slot(void)
{
    return synth_app_chord_state_active_valid_slot(&s_app.chords, s_app.play.arp_chord_valid);
}

static void stop_arp(void)
{
    if (s_app.play.arp_last_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.play.arp_last_slot));
    }
    synth_app_play_stop_arp(&s_app.play);
}

static void update_arp(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_ARPEGGIO) {
        return;
    }

    uint8_t slot = active_arp_slot();
    if (slot == SYNTH_APP_PLAY_NO_SLOT) {
        stop_arp();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.play.arp_next_tick) < 0) {
        return;
    }

    const synth_chord_t *source = &s_app.play.arp_chords[slot];
    if (source->note_count == 0) {
        return;
    }

    if (s_app.play.arp_last_slot != SYNTH_APP_PLAY_NO_SLOT && s_app.play.arp_last_slot != slot) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.play.arp_last_slot));
    }

    synth_chord_t step = {0};
    synth_app_build_arp_step_chord(s_app.play.arp_pattern, s_app.play.arp_step_index, source, &step);

    synth_chord_t output = step;
    if (s_app.play.arp_chord_mode == SYNTH_ARP_CHORD_MODE_CHORD_ARP) {
        output = *source;
        synth_app_append_step_notes(&output, &step);
    } else if (s_app.play.arp_chord_mode == SYNTH_ARP_CHORD_MODE_RHYTHM_ARP) {
        output = (s_app.play.arp_step_index & 1U) == 0 ? *source : step;
        if ((s_app.play.arp_step_index & 1U) == 0) {
            synth_app_append_step_notes(&output, &step);
        }
    }

    esp_err_t err = synth_amy_chord_on(slot, &output);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to play arp step: %s", esp_err_to_name(err));
        return;
    }
    synth_app_chord_state_remember_voice_anchor(&s_app.chords, &output);

    char note_name[48];
    synth_app_format_chord_notes(&step, note_name, sizeof(note_name));
    ESP_LOGD(TAG,
             "arp %s slot=%u pattern=%s rate=%s mode=%s step=%lu notes=%s",
             source->name,
             (unsigned)(slot + 1),
             synth_arp_pattern_name(s_app.play.arp_pattern),
             synth_rhythm_rate_name(s_app.play.arp_rate),
             synth_arp_chord_mode_name(s_app.play.arp_chord_mode),
             (unsigned long)s_app.play.arp_step_index,
             note_name);

    s_app.play.arp_last_slot = slot;
    s_app.play.arp_next_tick =
        now + pdMS_TO_TICKS(synth_app_rhythm_interval_ms(s_app.play.arp_rate,
                                                         current_bpm(),
                                                         s_app.play.arp_step_index));
    ++s_app.play.arp_step_index;
}

static void stop_repeat(void)
{
    if (s_app.play.repeat_active && s_app.play.repeat_slot != SYNTH_APP_PLAY_NO_SLOT) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.play.repeat_slot));
    }
    synth_app_play_stop_repeat(&s_app.play);
}

static void update_repeat(void)
{
    if (s_app.mode != SYNTH_PLAY_MODE_REPEAT) {
        return;
    }

    uint8_t slot = synth_app_chord_state_active_valid_slot(&s_app.chords, s_app.chords.held_chord_valid);
    if (slot == SYNTH_APP_PLAY_NO_SLOT) {
        stop_repeat();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_app.play.repeat_next_tick) < 0) {
        return;
    }

    uint32_t interval_ms = synth_app_rhythm_interval_ms(s_app.play.repeat_rate, current_bpm(), 0);
    TickType_t gate_ticks = pdMS_TO_TICKS(interval_ms / 2U);
    if (gate_ticks < pdMS_TO_TICKS(20)) {
        gate_ticks = pdMS_TO_TICKS(20);
    }

    if (s_app.play.repeat_gate_open) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(s_app.play.repeat_slot));
        s_app.play.repeat_gate_open = false;
        s_app.play.repeat_next_tick = now + gate_ticks;
        return;
    }

    s_app.play.repeat_slot = slot;
    esp_err_t err = synth_amy_chord_on(slot, &s_app.chords.held_chords[slot]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to repeat chord: %s", esp_err_to_name(err));
        return;
    }
    synth_app_chord_state_remember_voice_anchor(&s_app.chords, &s_app.chords.held_chords[slot]);
    s_app.play.repeat_active = true;
    s_app.play.repeat_gate_open = true;
    s_app.play.repeat_next_tick = now + gate_ticks;
}

static void update_lfo(void)
{
    float pitch_semitones = 0.0f;
    if (!synth_app_lfo_pitch(&s_app.lfo, s_app.fx.lfo_level, xTaskGetTickCount(), &pitch_semitones)) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_lfo_pitch(pitch_semitones));
}

static void update_tremolo(void)
{
    float amount = 0.0f;
    if (!synth_app_tremolo_amount(&s_app.tremolo,
                                  s_app.fx.tremolo_level,
                                  current_bpm(),
                                  xTaskGetTickCount(),
                                  &amount)) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_tremolo_amount(amount));
}

static void warn_unimplemented_mode(synth_play_mode_t mode)
{
    ESP_LOGW(TAG, "play mode %s is visible in UI but not implemented yet", synth_play_mode_name(mode));
    set_status("WARN MODE TODO");
}

static void log_chord_event(const char *verb, const synth_chord_t *chord)
{
#if ESP_POCKET_AUDIO_SYNTH_CHORD_LOG_ENABLED
    if (chord == NULL) {
        return;
    }

    char note_names[96];
    synth_app_format_chord_notes(chord, note_names, sizeof(note_names));
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
#else
    (void)verb;
    (void)chord;
#endif
}

static bool build_play_layer(synth_chord_t *layer)
{
    uint8_t slash_bass_slot = synth_app_bass_slot_for_chord(UINT8_MAX,
                                                            s_app.chords.held_mask,
                                                            s_app.bass_mode,
                                                            s_app.slash_bass_slot);
    return synth_app_build_play_layer(s_app.chords.held_chords,
                                      s_app.chords.held_chord_valid,
                                      s_app.chords.held_mask,
                                      s_app.bass_mode,
                                      slash_bass_slot,
                                      layer);
}

static void render_play_layer(void)
{
    synth_chord_t layer = {0};
    if (!build_play_layer(&layer)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(0));
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
    synth_app_chord_state_remember_voice_anchor(&s_app.chords, &layer);

    log_chord_event("play", &layer);
    strlcpy(s_app.chord_name, layer.name, sizeof(s_app.chord_name));
    set_status(layer.note_count > 3 ? "LAYER" : "PLAYING");
    update_ui();
}

static void play_degree(uint8_t degree)
{
    apply_pending_sound_now();

    synth_chord_t chord = {0};
    if (!build_chord(degree, &chord)) {
        ESP_LOGW(TAG, "failed to build chord degree=%u", degree);
        return;
    }

    uint8_t slot = slot_from_degree(degree);
    s_app.chords.held_chords[slot] = chord;
    s_app.chords.held_chord_valid[slot] = true;

    if (!synth_play_mode_is_implemented(s_app.mode)) {
        warn_unimplemented_mode(s_app.mode);
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        synth_app_play_queue_arp_source(&s_app.play, slot, &chord, xTaskGetTickCount());
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
        synth_app_chord_state_remember_voice_anchor(&s_app.chords, &chord);
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_LEAD) {
        synth_chord_t note = {0};
        synth_app_make_root_note_chord(&chord, &note);
        esp_err_t err = synth_amy_chord_on(slot, &note);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to play lead note: %s", esp_err_to_name(err));
            return;
        }
        synth_app_chord_state_remember_voice_anchor(&s_app.chords, &note);
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
        synth_app_chord_state_remember_voice_anchor(&s_app.chords, &chord);
        log_chord_event("drone", &chord);
        strlcpy(s_app.chord_name, chord.name, sizeof(s_app.chord_name));
        set_status("DRONE");
        update_ui();
        return;
    }

    if (s_app.mode == SYNTH_PLAY_MODE_REPEAT) {
        synth_app_play_queue_repeat_source(&s_app.play, slot, xTaskGetTickCount());
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
        if ((s_app.chords.held_mask & (1U << slot)) != 0) {
            play_degree((uint8_t)(slot + 1));
        }
    }
}

static bool sound_change_should_apply_immediately(void)
{
    return s_app.chords.held_mask != 0 ||
           s_app.play.strum_active ||
           s_app.drone_active ||
           synth_app_play_arp_clock_active(&s_app.play) ||
           synth_app_play_repeat_clock_active(&s_app.play);
}

static void apply_sound_to_engine(synth_sound_t sound)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(sound));
    apply_current_envelope_if_modified();
    apply_current_glide();
}

static void apply_sound_immediately(synth_sound_t sound)
{
    synth_app_sound_mark_applied(&s_app.sound, sound);
    apply_sound_to_engine(sound);
}

static void schedule_sound_apply(synth_sound_t sound)
{
    synth_app_sound_schedule(&s_app.sound,
                             sound,
                             xTaskGetTickCount(),
                             SOUND_BROWSE_APPLY_TICKS);
}

static void apply_pending_sound_now(void)
{
    if (!synth_app_sound_has_pending(&s_app.sound)) {
        return;
    }

    apply_sound_immediately(synth_app_sound_pending(&s_app.sound));
}

static void update_pending_sound(void)
{
    if (!synth_app_sound_has_pending(&s_app.sound) || sound_change_should_apply_immediately()) {
        return;
    }

    if (!synth_app_sound_pending_due(&s_app.sound, xTaskGetTickCount())) {
        return;
    }

    apply_pending_sound_now();
    set_status("SOUND READY");
    update_ui();
}

static void apply_mixer_result(const synth_app_mixer_result_t *result)
{
    if (result == NULL || !result->handled) {
        return;
    }

    if (result->focus_track < SYNTH_LOOPER_TRACK_COUNT) {
        s_app.chords.focus_slot = result->focus_track;
    }

    switch (result->action) {
    case SYNTH_APP_MIXER_ACTION_TOGGLE_METRONOME:
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_looper_toggle_metronome());
        break;
    case SYNTH_APP_MIXER_ACTION_TOGGLE_MUTE:
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_looper_toggle_track_mute(result->action_track));
        break;
    case SYNTH_APP_MIXER_ACTION_TOGGLE_SOLO:
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_looper_toggle_track_solo(result->action_track));
        break;
    case SYNTH_APP_MIXER_ACTION_TOGGLE_PAUSE:
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_looper_toggle_pause());
        break;
    case SYNTH_APP_MIXER_ACTION_NONE:
    default:
        break;
    }

    if (result->status != NULL) {
        set_status(result->status);
    }
    if (result->ui_dirty) {
        update_ui();
    }
}

static bool handle_mixer_chord_event(const control_event_t *event, uint8_t degree)
{
    if (s_app.mode != SYNTH_PLAY_MODE_MIXER) {
        return false;
    }

    synth_app_mixer_result_t result =
        synth_app_mixer_handle_chord_event(&s_app.mixer,
                                           event,
                                           degree,
                                           synth_app_wheel_percent(&s_app.wheel));
    apply_mixer_result(&result);
    return result.handled;
}

static void apply_looper_result(const synth_app_looper_result_t *result)
{
    if (result == NULL || (!result->ui_dirty && result->action == SYNTH_APP_LOOPER_ACTION_NONE)) {
        return;
    }

    esp_err_t err = ESP_OK;
    switch (result->action) {
    case SYNTH_APP_LOOPER_ACTION_CLICK:
        err = synth_looper_click(current_bpm());
        break;
    case SYNTH_APP_LOOPER_ACTION_CLEAR_ALL:
        err = synth_looper_clear_all();
        break;
    case SYNTH_APP_LOOPER_ACTION_SELECT_TRACK:
        err = synth_looper_select_track(result->action_steps);
        break;
    case SYNTH_APP_LOOPER_ACTION_ADJUST_BARS:
        err = synth_looper_adjust_bars(result->action_steps);
        break;
    case SYNTH_APP_LOOPER_ACTION_TOGGLE_PAUSE:
        err = synth_looper_toggle_pause();
        break;
    case SYNTH_APP_LOOPER_ACTION_NONE:
    default:
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "looper action failed: %s", esp_err_to_name(err));
        set_status("LOOPER ERR");
    } else if (result->status != NULL) {
        set_status(result->status);
    }
    if (result->ui_dirty) {
        update_ui();
    }
}

static void release_degree(uint8_t degree)
{
    uint8_t slot = slot_from_degree(degree);
    s_app.chords.held_mask &= (uint8_t)~(1U << slot);
    s_app.chords.held_chord_valid[slot] = false;
    synth_app_play_clear_arp_source(&s_app.play, slot);
    if (s_app.slash_bass_slot == slot) {
        uint8_t replacement = synth_app_first_held_slot_except(s_app.chords.held_mask, UINT8_MAX);
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

    if (s_app.mode == SYNTH_PLAY_MODE_STRUM && s_app.play.strum_slot == slot) {
        stop_strum();
    } else if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        if (s_app.chords.held_mask == 0) {
            stop_arp();
        }
    } else if (s_app.mode == SYNTH_PLAY_MODE_REPEAT) {
        if (s_app.chords.held_mask == 0) {
            stop_repeat();
        }
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_chord_off(slot));
    }

    if (s_app.chords.held_mask == 0) {
        stop_arp();
        if (s_app.mode != SYNTH_PLAY_MODE_DRONE) {
            stop_repeat();
        }
        set_status("READY");
    } else if (s_app.mode == SYNTH_PLAY_MODE_PLAY) {
        uint8_t next_slot = synth_app_chord_state_active_valid_slot(&s_app.chords, s_app.chords.held_chord_valid);
        if (next_slot != SYNTH_APP_PLAY_NO_SLOT) {
            play_degree((uint8_t)(next_slot + 1));
        }
    }
    update_ui();
}

static void sync_play_modifier_from_joystick(void)
{
    control_joystick_state_t joystick = {0};
    if (controls_joystick_read(&joystick) != ESP_OK) {
        return;
    }

    s_app.joystick_direction = joystick.direction;
    s_app.last_play_direction = joystick.direction;

    synth_chord_modifier_t modifier =
        synth_chord_modifier_from_joystick(s_app.joystick_mode, joystick.direction);
    if (modifier == s_app.modifier) {
        return;
    }

    s_app.modifier = modifier;
    ESP_LOGI(TAG, "joystick modifier=%s", synth_chord_modifier_name(modifier));
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
            apply_sound_immediately(synth_sound_quick_select(degree));
            set_status("SOUND SELECTED");
            refresh_held_chords();
            update_ui();
            return;
        }
        if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
            if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO && degree <= 6) {
                s_app.play.arp_pattern = (synth_arp_pattern_t)(degree - 1);
                s_app.play.arp_step_index = 0;
                ESP_LOGI(TAG, "arp pattern=%s", synth_arp_pattern_name(s_app.play.arp_pattern));
                set_status(synth_arp_pattern_name(s_app.play.arp_pattern));
            } else if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO && degree == 7) {
                s_app.play.arp_chord_mode = (synth_arp_chord_mode_t)((s_app.play.arp_chord_mode + 1U) %
                                                                SYNTH_ARP_CHORD_MODE_COUNT);
                ESP_LOGI(TAG, "arp chord mode=%s", synth_arp_chord_mode_name(s_app.play.arp_chord_mode));
                set_status(synth_arp_chord_mode_name(s_app.play.arp_chord_mode));
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

        if (handle_mixer_chord_event(event, degree)) {
            return;
        }

        if (s_app.bass_mode == SYNTH_BASS_MODE_SLASH &&
            s_app.chords.held_mask != 0 &&
            (s_app.chords.held_mask & (1U << slot)) == 0) {
            uint8_t bass_slot = synth_app_chord_state_active_valid_slot(&s_app.chords, s_app.chords.held_chord_valid);
            if (bass_slot < SYNTH_CHORD_BUTTON_COUNT) {
                s_app.slash_bass_slot = bass_slot;
            }
        } else if (s_app.bass_mode == SYNTH_BASS_MODE_SLASH &&
                   s_app.chords.held_mask == 0) {
            s_app.slash_bass_slot = slot;
        }

        sync_play_modifier_from_joystick();
        s_app.chords.held_mask |= 1U << slot;
        s_app.chords.focus_slot = slot;
        play_degree(degree);
    } else {
        if (handle_mixer_chord_event(event, degree)) {
            return;
        }
        release_degree(degree);
    }
}

static void step_key(int8_t semitones)
{
    synth_key_t next_key = s_app.key;
    int8_t next_octave = s_app.global_octave_shift;
    if (!synth_app_key_step(s_app.key,
                            s_app.global_octave_shift,
                            semitones,
                            &next_key,
                            &next_octave)) {
        return;
    }

    s_app.key = next_key;
    s_app.global_octave_shift = next_octave;
    synth_app_chord_state_clear_voice_anchor(&s_app.chords);
    ESP_LOGI(TAG, "key=%s octave=%d", synth_key_name(s_app.key), s_app.global_octave_shift);
    set_status("KEY CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_global_octave(int8_t octaves)
{
    int8_t next = s_app.global_octave_shift;
    if (!synth_app_global_octave_step(s_app.global_octave_shift, octaves, &next)) {
        return;
    }

    s_app.global_octave_shift = next;
    synth_app_chord_state_clear_voice_anchor(&s_app.chords);
    ESP_LOGI(TAG, "global octave=%d", s_app.global_octave_shift);
    set_status("OCTAVE CHANGED");
    refresh_held_chords();
    update_ui();
}

static void step_focused_button_octave(int8_t octaves)
{
    if ((s_app.chords.held_mask & (1U << s_app.chords.focus_slot)) == 0) {
        return;
    }

    int8_t next = s_app.button_octave_shift[s_app.chords.focus_slot];
    if (!synth_app_button_octave_step(s_app.button_octave_shift[s_app.chords.focus_slot],
                                      octaves,
                                      &next)) {
        return;
    }

    s_app.button_octave_shift[s_app.chords.focus_slot] = next;
    synth_app_chord_state_clear_voice_anchor(&s_app.chords);
    ESP_LOGI(TAG, "chord %u octave=%d", (unsigned)(s_app.chords.focus_slot + 1), next);
    set_status("BUTTON OCTAVE");
    play_degree((uint8_t)(s_app.chords.focus_slot + 1));
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
        if (s_app.chords.held_mask != 0) {
            step_focused_button_octave(1);
        } else {
            step_global_octave(1);
        }
        break;
    case CONTROL_JOYSTICK_DOWN:
        if (s_app.chords.held_mask != 0) {
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
    synth_sound_t current = synth_app_sound_current(&s_app.sound);
    synth_sound_t next = synth_sound_step(current, steps);
    if (next == current) {
        return;
    }

    if (sound_change_should_apply_immediately()) {
        apply_sound_immediately(next);
        refresh_held_chords();
    } else {
        schedule_sound_apply(next);
    }
    set_status("SOUND CHANGED");
    update_ui();
}

static void step_effect(int8_t steps)
{
    synth_app_effects_step_selected(&s_app.fx, steps);
    set_status("EFFECT SELECT");
    update_ui();
}

static void step_joystick_mode(void)
{
    s_app.joystick_mode = synth_joystick_mode_step(s_app.joystick_mode, 1);
    s_app.modifier = s_app.chords.held_mask != 0 ?
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
        synth_app_chord_state_clear_voice_anchor(&s_app.chords);
    }
    ESP_LOGI(TAG, "voice leading=%s", s_app.voice_leading_enabled ? "on" : "off");
    set_status(s_app.voice_leading_enabled ? "VOICE LEAD ON" : "VOICE LEAD OFF");
    refresh_held_chords();
    update_ui();
}

static void cycle_focused_chord_inversion(void)
{
    if (s_app.chords.held_mask == 0) {
        return;
    }

    uint8_t slot = synth_app_chord_state_focused_slot(&s_app.chords);
    if (slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return;
    }

    int8_t next = (int8_t)(s_app.chords.chord_inversion[slot] + 1);
    if (next > CHORD_INVERSION_MAX) {
        next = CHORD_INVERSION_MIN;
    }
    s_app.chords.chord_inversion[slot] = next;
    synth_app_chord_state_clear_voice_anchor(&s_app.chords);
    ESP_LOGI(TAG,
             "chord %u inversion=%d",
             (unsigned)(slot + 1),
             s_app.chords.chord_inversion[slot]);
    set_status("INVERSION");
    play_degree((uint8_t)(slot + 1));
    update_ui();
}

static void toggle_focused_chord_lock(void)
{
    if (s_app.chords.held_mask == 0 || s_app.modifier == SYNTH_CHORD_MOD_NONE) {
        return;
    }

    uint8_t slot = synth_app_chord_state_focused_slot(&s_app.chords);
    if (slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return;
    }

    if (s_app.chords.locked_modifier[slot] == s_app.modifier) {
        s_app.chords.locked_modifier[slot] = SYNTH_CHORD_MOD_NONE;
        ESP_LOGI(TAG, "chord %u lock cleared", (unsigned)(slot + 1));
        set_status("LOCK OFF");
    } else {
        s_app.chords.locked_modifier[slot] = s_app.modifier;
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
    synth_app_effects_step_lfo_level(&s_app.fx);
    synth_app_modulation_reset(&s_app.lfo, xTaskGetTickCount());

    esp_err_t err = synth_amy_set_lfo_enabled(s_app.fx.enabled[PLAY_UI_EFFECT_LFO]);
    if (err == ESP_OK) {
        err = synth_amy_set_lfo_pitch(0.0f);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply LFO: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG,
             "lfo=%s level=%u depth=%.2f semitones",
             s_app.fx.enabled[PLAY_UI_EFFECT_LFO] ? "on" : "off",
             s_app.fx.lfo_level,
             synth_app_lfo_depth_semitones(s_app.fx.lfo_level));
    set_status(s_app.fx.enabled[PLAY_UI_EFFECT_LFO] ? "LFO ON" : "LFO OFF");
    update_ui();
}

static float delay_ms_for_level(uint8_t level)
{
    return synth_app_delay_ms_for_level(level, current_bpm());
}

static void apply_delay_level(void)
{
    float delay_ms = delay_ms_for_level(s_app.fx.delay_level);
    bool enabled = s_app.fx.delay_level > 0;
    s_app.fx.enabled[PLAY_UI_EFFECT_DELAY] = enabled;
    ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_delay_enabled(enabled, delay_ms));
}

static void step_delay_level(void)
{
    synth_app_effects_step_delay_level(&s_app.fx);
    apply_delay_level();

    ESP_LOGI(TAG,
             "delay=%s level=%s time=%.1f ms",
             s_app.fx.enabled[PLAY_UI_EFFECT_DELAY] ? "on" : "off",
             synth_app_delay_level_name(s_app.fx.delay_level),
             delay_ms_for_level(s_app.fx.delay_level));
    set_status(s_app.fx.enabled[PLAY_UI_EFFECT_DELAY] ? "DELAY ON" : "DELAY OFF");
    update_ui();
}

static void step_tremolo_level(void)
{
    synth_app_effects_step_tremolo_level(&s_app.fx);
    synth_app_modulation_reset(&s_app.tremolo, xTaskGetTickCount());

    esp_err_t err = synth_amy_set_tremolo_enabled(s_app.fx.enabled[PLAY_UI_EFFECT_TREMOLO]);
    if (err == ESP_OK) {
        err = synth_amy_set_tremolo_amount(0.0f);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply tremolo: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG,
             "tremolo=%s level=%s rate=%.2f Hz",
             s_app.fx.enabled[PLAY_UI_EFFECT_TREMOLO] ? "on" : "off",
             synth_app_tremolo_level_name(s_app.fx.tremolo_level),
             synth_app_tremolo_rate_hz(s_app.fx.tremolo_level, current_bpm()));
    set_status(s_app.fx.enabled[PLAY_UI_EFFECT_TREMOLO] ? "TREM ON" : "TREM OFF");
    update_ui();
}

static void step_voice_level(void)
{
    synth_app_effects_step_voice_level(&s_app.fx);
    uint8_t voice_limit = synth_app_voice_limit(s_app.fx.voice_level);
    esp_err_t err = synth_amy_set_voice_limit(voice_limit);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply voice count: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG,
             "voices=%s limit=%u",
             synth_app_voice_level_name(s_app.fx.voice_level),
             (unsigned)voice_limit);
    set_status("VOICES");
    refresh_held_chords();
    update_ui();
}

static void step_adsr_preset(void)
{
    synth_app_effects_step_adsr_preset(&s_app.fx);
    synth_amy_envelope_t preset = synth_app_adsr_preset_envelope(s_app.fx.adsr_preset);
    const char *preset_name = synth_app_adsr_preset_name(s_app.fx.adsr_preset);
    apply_current_envelope();

    ESP_LOGI(TAG,
             "adsr=%s attack=%lu decay=%lu sustain=%.2f release=%lu",
             preset_name,
             (unsigned long)s_app.fx.attack_ms,
             (unsigned long)preset.decay_ms,
             preset.sustain_level,
             (unsigned long)s_app.fx.release_ms);
    set_status(preset_name);
    refresh_held_chords();
    update_ui();
}

static void step_glide_level(void)
{
    synth_app_effects_step_glide_level(&s_app.fx);
    apply_current_glide();

    ESP_LOGI(TAG,
             "glide=%s portamento=%ums",
             synth_app_glide_level_name(s_app.fx.glide_level),
             (unsigned)current_glide_ms());
    set_status(s_app.fx.glide_level == 0 ? "GLIDE OFF" : "GLIDE ON");
    update_ui();
}

static void toggle_current_effect(void)
{
    play_ui_effect_t effect = synth_app_effects_selected(&s_app.fx);
    if (effect == PLAY_UI_EFFECT_JOYSTICK) {
        step_joystick_mode();
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
    if (effect == PLAY_UI_EFFECT_VOICE_COUNT) {
        step_voice_level();
        return;
    }
    if (effect == PLAY_UI_EFFECT_ADSR) {
        step_adsr_preset();
        return;
    }
    if (effect == PLAY_UI_EFFECT_GLIDE) {
        step_glide_level();
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

    synth_app_effects_toggle_enabled(&s_app.fx, effect);
    esp_err_t err = ESP_OK;
    switch (effect) {
    case PLAY_UI_EFFECT_REVERB:
        err = synth_amy_set_reverb_enabled(s_app.fx.enabled[effect]);
        break;
    case PLAY_UI_EFFECT_CHORUS:
        err = synth_amy_set_chorus_enabled(s_app.fx.enabled[effect]);
        break;
    case PLAY_UI_EFFECT_STEREO:
        err = synth_amy_set_stereo_enabled(s_app.fx.enabled[effect]);
        break;
    case PLAY_UI_EFFECT_FILTER:
        err = synth_amy_set_filter_enabled(s_app.fx.enabled[effect]);
        break;
    case PLAY_UI_EFFECT_FLANGER:
        err = synth_amy_set_flanger_enabled(s_app.fx.enabled[effect]);
        break;
    default:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to apply %s: %s", play_ui_effect_name(effect), esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "%s=%s", play_ui_effect_name(effect), s_app.fx.enabled[effect] ? "on" : "off");
    set_status(s_app.fx.enabled[effect] ? "FX ON" : "FX OFF");
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
    if (!synth_app_tempo_step(&s_app.tempo, delta)) {
        return;
    }

    ESP_LOGI(TAG, "bpm=%u", current_bpm());
    if (s_app.fx.enabled[PLAY_UI_EFFECT_DELAY]) {
        apply_delay_level();
    }
    set_status("BPM CHANGED");
    update_ui();
}

static void cycle_mode_param(void)
{
    switch (s_app.mode) {
    case SYNTH_PLAY_MODE_STRUM:
        s_app.play.strum_speed = (synth_strum_speed_t)((s_app.play.strum_speed + 1U) % SYNTH_STRUM_SPEED_COUNT);
        ESP_LOGI(TAG,
                 "strum speed=%s delay=%ums",
                 synth_strum_speed_name(s_app.play.strum_speed),
                 synth_strum_speed_delay_ms(s_app.play.strum_speed));
        set_status(synth_strum_speed_name(s_app.play.strum_speed));
        break;
    case SYNTH_PLAY_MODE_ARPEGGIO:
        s_app.play.arp_rate = (synth_rhythm_rate_t)((s_app.play.arp_rate + 1U) % SYNTH_RHYTHM_RATE_COUNT);
        s_app.play.arp_step_index = 0;
        ESP_LOGI(TAG, "arp rate=%s", synth_rhythm_rate_name(s_app.play.arp_rate));
        set_status(synth_rhythm_rate_name(s_app.play.arp_rate));
        break;
    case SYNTH_PLAY_MODE_REPEAT:
        s_app.play.repeat_rate = (synth_rhythm_rate_t)((s_app.play.repeat_rate + 1U) % SYNTH_RHYTHM_RATE_COUNT);
        s_app.play.repeat_next_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "repeat rate=%s", synth_rhythm_rate_name(s_app.play.repeat_rate));
        set_status(synth_rhythm_rate_name(s_app.play.repeat_rate));
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

static synth_app_wheel_target_t current_wheel_target(void)
{
    if (controls_switch_is_pressed(CONTROL_KEY_MENU)) {
        return SYNTH_APP_WHEEL_TARGET_ATTACK;
    }
    if (controls_switch_is_pressed(CONTROL_SOUND_MENU)) {
        return SYNTH_APP_WHEEL_TARGET_RELEASE;
    }
    if (controls_switch_is_pressed(CONTROL_MODE_MENU)) {
        return SYNTH_APP_WHEEL_TARGET_FILTER;
    }
    if (s_app.mode == SYNTH_PLAY_MODE_MIXER &&
        synth_app_mixer_first_held_track(&s_app.mixer) < SYNTH_LOOPER_TRACK_COUNT) {
        return SYNTH_APP_WHEEL_TARGET_LOOPER_TRACK;
    }
    return SYNTH_APP_WHEEL_TARGET_VOLUME;
}

static bool apply_wheel(const control_wheel_state_t *wheel)
{
    if (wheel == NULL || !wheel->present) {
        return false;
    }

    synth_app_wheel_target_t target = current_wheel_target();
    if (target == SYNTH_APP_WHEEL_TARGET_LOOPER_TRACK) {
        uint8_t track = synth_app_mixer_first_held_track(&s_app.mixer);
        if (!synth_app_mixer_wheel_moved(&s_app.mixer,
                                         track,
                                         wheel->percent,
                                         WHEEL_CHANGE_THRESHOLD_PERCENT)) {
            return false;
        }
    }
    if (!synth_app_wheel_should_apply(&s_app.wheel,
                                      target,
                                      wheel->percent,
                                      WHEEL_CHANGE_THRESHOLD_PERCENT)) {
        return false;
    }

    if (target == SYNTH_APP_WHEEL_TARGET_ATTACK) {
        synth_app_effects_set_attack_ms(&s_app.fx, synth_app_attack_ms_from_wheel(wheel->percent));
        apply_current_envelope();
        set_status("ATTACK");
    } else if (target == SYNTH_APP_WHEEL_TARGET_RELEASE) {
        synth_app_effects_set_release_ms(&s_app.fx, synth_app_release_ms_from_wheel(wheel->percent));
        apply_current_envelope();
        set_status("RELEASE");
    } else if (target == SYNTH_APP_WHEEL_TARGET_FILTER) {
        float cutoff_hz = synth_app_filter_cutoff_hz_from_wheel(wheel->percent);
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_filter_cutoff(cutoff_hz));
        set_status("FILTER");
    } else if (target == SYNTH_APP_WHEEL_TARGET_LOOPER_TRACK) {
        uint8_t track = synth_app_mixer_first_held_track(&s_app.mixer);
        if (track >= SYNTH_LOOPER_TRACK_COUNT) {
            return false;
        }
        synth_app_mixer_mark_action(&s_app.mixer, track);
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_looper_set_track_volume(track, wheel->percent));
        set_status("LOOP VOL");
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_volume(wheel->value));
        synth_app_wheel_set_volume_percent(&s_app.wheel, wheel->percent);
        ESP_LOGI(TAG,
                 "master volume=%u%%",
                 (unsigned)synth_app_wheel_volume_percent(&s_app.wheel));
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

    bool display_changed = synth_app_wheel_update(&s_app.wheel, wheel.present, wheel.percent);
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
        s_app.drone_slot = SYNTH_APP_PLAY_NO_SLOT;
    }
    if (previous == SYNTH_PLAY_MODE_MIXER) {
        synth_app_mixer_reset(&s_app.mixer);
    }
    synth_app_play_clear_arp_sources(&s_app.play, xTaskGetTickCount());

    s_app.mode = next;
    s_app.mode_bpm_submenu = false;
    apply_current_envelope_if_modified();
    if (s_app.mode == SYNTH_PLAY_MODE_MIXER) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        synth_app_chord_state_clear_held(&s_app.chords);
        synth_app_play_clear_arp_sources(&s_app.play, xTaskGetTickCount());
        synth_app_looper_hide(&s_app.looper);
        set_status("MIXER");
    } else if (s_app.mode == SYNTH_PLAY_MODE_ARPEGGIO) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        synth_app_play_clear_arp_sources(&s_app.play, xTaskGetTickCount());
        refresh_held_chords();
    } else if (synth_play_mode_is_implemented(s_app.mode) && s_app.chords.held_mask != 0) {
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

static void handle_looper_click(void)
{
    synth_app_looper_result_t result =
        synth_app_looper_click(&s_app.looper, xTaskGetTickCount());
    apply_looper_result(&result);
}

static void handle_looper_clear_all(void)
{
    synth_app_looper_result_t result = synth_app_looper_clear_all();
    apply_looper_result(&result);
}

static void update_looper_joystick(control_joystick_direction_t direction)
{
    synth_app_looper_result_t result =
        synth_app_looper_handle_joystick(&s_app.looper,
                                         direction,
                                         synth_looper_current_track_is_waiting());
    apply_looper_result(&result);
}

static void update_looper_periodic_ui(void)
{
    TickType_t now = xTaskGetTickCount();
    bool screen_visible = synth_app_looper_visible(&s_app.looper) ||
                          s_app.mode == SYNTH_PLAY_MODE_MIXER;
    if (synth_app_looper_periodic_due(&s_app.looper, screen_visible, now)) {
        update_ui();
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

    if (synth_app_looper_visible(&s_app.looper) && s_app.chords.held_mask == 0) {
        update_looper_joystick(joystick.direction);
        s_app.last_play_direction = CONTROL_JOYSTICK_CENTER;
        return;
    }
    synth_app_looper_reset_direction(&s_app.looper);

    if (s_app.mode == SYNTH_PLAY_MODE_MIXER) {
        s_app.last_play_direction = joystick.direction;
        if (s_app.modifier != SYNTH_CHORD_MOD_NONE) {
            s_app.modifier = SYNTH_CHORD_MOD_NONE;
            update_ui();
        }
        return;
    }

    if (s_app.joystick_mode == SYNTH_JOYSTICK_MODE_CHROMATIC &&
        s_app.chords.held_mask == 0 &&
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

    if (s_app.chords.held_mask == 0) {
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
    synth_app_tempo_tap_result_t tap =
        synth_app_tempo_tap(&s_app.tempo, xTaskGetTickCount());
    if (!tap.changed) {
        return;
    }

    if (s_app.fx.enabled[PLAY_UI_EFFECT_DELAY]) {
        apply_delay_level();
    }
    ESP_LOGI(TAG, "tap tempo bpm=%u taps=%u", tap.bpm, tap.tap_count);
    set_status("TAP TEMPO");
    update_ui();
}

static void handle_mixer_joystick_click(void)
{
    synth_app_mixer_result_t result =
        synth_app_mixer_handle_joystick_click(&s_app.mixer);
    apply_mixer_result(&result);
}

static void handle_menu_event(const control_event_t *event)
{
    if (event->type != CONTROL_EVENT_SWITCH_PRESSED) {
        if (event->sw == CONTROL_KEY_MENU) {
            synth_app_looper_finish_menu_hold(&s_app.looper);
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
            if (synth_app_looper_visible(&s_app.looper)) {
                synth_app_looper_mark_menu_clear_used(&s_app.looper);
                handle_looper_clear_all();
            } else {
                set_status("KEY MENU");
                update_ui();
            }
        } else if (s_app.mode == SYNTH_PLAY_MODE_MIXER) {
            handle_mixer_joystick_click();
        } else {
            handle_looper_click();
        }
    } else if (event->sw == CONTROL_KEY_MENU ||
               event->sw == CONTROL_SOUND_MENU ||
               event->sw == CONTROL_MODE_MENU) {
        if (s_app.chords.held_mask != 0) {
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

        if (event->sw == CONTROL_KEY_MENU && synth_app_looper_visible(&s_app.looper)) {
            synth_app_looper_begin_menu_hold(&s_app.looper);
        } else {
            synth_app_looper_hide(&s_app.looper);
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

    ESP_LOGI(TAG, "chord synth app started key=%s", synth_key_name(s_app.key));

    while (true) {
        control_event_t event = {0};
        while (controls_pop_event(&event) == ESP_OK) {
            handle_menu_event(&event);
            handle_chord_event(&event);
        }

        update_joystick();
        update_pending_sound();
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
    ESP_RETURN_ON_ERROR(synth_amy_set_sound(synth_app_sound_current(&s_app.sound)),
                        TAG,
                        "AMY sound setup failed");
    apply_current_glide();
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
