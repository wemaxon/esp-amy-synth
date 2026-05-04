#include "app_synth.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "periph_button.h"
#include "board.h"
#include "app_beat.h"
#include "amy_esp_adf.h"

static const char *TAG = "app_synth";

static const uint8_t MELODY_SYNTH = 1;
static const uint16_t MELODY_NOTE_DURATION_MS = 320;
static const uint16_t MELODY_STEP_PULSES = 24;
static const uint8_t PENTATONIC_BASE_NOTE = 60;
static const uint8_t PENTATONIC_INTERVALS[] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21};
static const float HELD_CHORD_VELOCITIES[] = {0.52f, 0.40f, 0.30f};
static const float MELODY_VELOCITY = 0.72f;
static const float MASTER_VOLUME_TARGETS[] = {2.52f, 2.24f, 2.00f, 1.82f};
static const TickType_t BUTTON_DEBOUNCE_TICKS = pdMS_TO_TICKS(40);
static const TickType_t CHORD_MOTION_STEP_TICKS = pdMS_TO_TICKS(90);
static const TickType_t MELODY_FILTER_STEP_TICKS = pdMS_TO_TICKS(180);
static const float BUTTON_PANS[] = {0.18f, 0.30f, 0.42f, 0.58f, 0.70f, 0.82f};
static const float CHORD_PAN_SWAY = 0.07f;
static const float CHORD_MOTION_RATE = 0.28f;
static const float MELODY_FILTER_BASE_HZ = 210.0f;
static const float MELODY_FILTER_SWEEP_HZ = 430.0f;
static const float MELODY_FILTER_NOTE_TRACKING = 0.74f;
static const float MELODY_FILTER_ENV_AMOUNT = 1.70f;
static const float MELODY_FILTER_RESONANCE_BASE = 0.92f;
static const float MELODY_FILTER_RESONANCE_SWEEP = 0.30f;
static const float MELODY_FILTER_PHASE_STEP = 0.16f;
static const float TAU_F = 6.28318530718f;

typedef struct {
    int button_id;
    size_t scale_degree;
    uint8_t synth;
    bool is_held;
    uint8_t active_notes[3];
    TickType_t hold_started_tick;
    int last_cmd;
    TickType_t last_event_tick;
} button_chord_map_t;

typedef struct {
    uint64_t next_melody_pulse;
    TickType_t next_chord_motion_tick;
    TickType_t next_filter_tick;
    size_t melody_step;
    size_t phrase_index;
    uint32_t phrase_counter;
    float filter_phase;
} app_synth_state_t;

static app_synth_state_t s_state = {
    .next_melody_pulse = 0,
    .next_chord_motion_tick = 0,
    .next_filter_tick = 0,
    .melody_step = 0,
    .phrase_index = 0,
    .phrase_counter = 0,
    .filter_phase = 0.0f,
};

static button_chord_map_t BUTTON_MAP[] = {
    {BUTTON_REC_ID, 0, 20, false, {0}, 0, -1, 0},
    {BUTTON_MODE_ID, 1, 21, false, {0}, 0, -1, 0},
    {BUTTON_SET_ID, 2, 22, false, {0}, 0, -1, 0},
    {BUTTON_PLAY_ID, 3, 23, false, {0}, 0, -1, 0},
    {BUTTON_VOLDOWN_ID, 4, 24, false, {0}, 0, -1, 0},
    {BUTTON_VOLUP_ID, 5, 25, false, {0}, 0, -1, 0},
};

// Phrase variants all stay inside the same pentatonic pool so changes remain musical.
static const uint8_t MELODY_PHRASES[][8] = {
    {0, 2, 4, 2, 0, 2, 4, 7},
    {0, 2, 4, 7, 4, 2, 0, 2},
    {0, 2, 4, 2, 4, 7, 4, 2},
    {0, 2, 4, 7, 9, 7, 4, 2},
};

static button_chord_map_t *get_button_map(int button_id)
{
    for (size_t i = 0; i < sizeof(BUTTON_MAP) / sizeof(BUTTON_MAP[0]); ++i) {
        if (BUTTON_MAP[i].button_id == button_id) {
            return &BUTTON_MAP[i];
        }
    }
    return NULL;
}

static uint8_t melody_note_for_step(size_t phrase_index, size_t step_index)
{
    size_t phrase_count = sizeof(MELODY_PHRASES) / sizeof(MELODY_PHRASES[0]);
    size_t phrase_len = sizeof(MELODY_PHRASES[0]) / sizeof(MELODY_PHRASES[0][0]);
    uint8_t degree = MELODY_PHRASES[phrase_index % phrase_count][step_index % phrase_len];

    // Occasional light octave shimmer on strong beats to keep the dreamy feeling.
    if ((step_index % 4) == 3 && (esp_random() % 5) == 0) {
        degree += 5;
    }

    return PENTATONIC_BASE_NOTE + PENTATONIC_INTERVALS[degree];
}

static size_t active_pad_count(void)
{
    size_t active_count = 0;

    for (size_t i = 0; i < sizeof(BUTTON_MAP) / sizeof(BUTTON_MAP[0]); ++i) {
        if (BUTTON_MAP[i].is_held) {
            active_count++;
        }
    }

    return active_count;
}

static esp_err_t update_master_volume_for_density(void)
{
    size_t active_count = active_pad_count();
    size_t target_index = active_count;
    if (target_index >= (sizeof(MASTER_VOLUME_TARGETS) / sizeof(MASTER_VOLUME_TARGETS[0]))) {
        target_index = (sizeof(MASTER_VOLUME_TARGETS) / sizeof(MASTER_VOLUME_TARGETS[0])) - 1;
    }

    return amy_esp_adf_set_master_volume(MASTER_VOLUME_TARGETS[target_index]);
}

static float clamp_unit(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static esp_err_t update_chord_motion(TickType_t now_ticks)
{
    while ((int32_t)(now_ticks - s_state.next_chord_motion_tick) >= 0) {
        for (size_t i = 0; i < sizeof(BUTTON_MAP) / sizeof(BUTTON_MAP[0]); ++i) {
            button_chord_map_t *button = &BUTTON_MAP[i];
            if (!button->is_held) {
                continue;
            }

            float held_ms = (float)pdTICKS_TO_MS(now_ticks - button->hold_started_tick);
            float phase = ((float)i * 0.9f) + (held_ms * 0.001f * TAU_F * CHORD_MOTION_RATE);
            float pan = clamp_unit(BUTTON_PANS[i] + (CHORD_PAN_SWAY * sinf(phase)));
            ESP_ERROR_CHECK(amy_esp_adf_set_synth_pan(button->synth, pan));
        }

        s_state.next_chord_motion_tick += CHORD_MOTION_STEP_TICKS;
    }

    return ESP_OK;
}

static esp_err_t update_melody_filter_motion(TickType_t now_ticks)
{
    while ((int32_t)(now_ticks - s_state.next_filter_tick) >= 0) {
        float sweep = 0.5f + (0.5f * sinf(s_state.filter_phase));
        float resonance_sweep = 0.5f + (0.5f * sinf(s_state.filter_phase + 1.1f));
        float cutoff_hz = MELODY_FILTER_BASE_HZ + (MELODY_FILTER_SWEEP_HZ * sweep);
        float resonance = MELODY_FILTER_RESONANCE_BASE + (MELODY_FILTER_RESONANCE_SWEEP * resonance_sweep);

        ESP_ERROR_CHECK(amy_esp_adf_set_synth_filter_shape(
            MELODY_SYNTH,
            cutoff_hz,
            MELODY_FILTER_NOTE_TRACKING,
            MELODY_FILTER_ENV_AMOUNT,
            resonance
        ));

        s_state.filter_phase += MELODY_FILTER_PHASE_STEP;
        if (s_state.filter_phase > TAU_F) {
            s_state.filter_phase -= TAU_F;
        }
        s_state.next_filter_tick += MELODY_FILTER_STEP_TICKS;
    }

    return ESP_OK;
}

static void fill_chord_notes(const button_chord_map_t *button, uint8_t notes[3])
{
    for (size_t voice = 0; voice < 3; ++voice) {
        size_t note_index = button->scale_degree + (voice * 2);
        notes[voice] = PENTATONIC_BASE_NOTE + PENTATONIC_INTERVALS[note_index];
    }
}

static bool should_accept_button_event(button_chord_map_t *button, int cmd, TickType_t now_ticks)
{
    if (button->last_cmd == cmd && (now_ticks - button->last_event_tick) < BUTTON_DEBOUNCE_TICKS) {
        ESP_LOGI(TAG, "Ignoring bounce on button %d cmd %d", button->button_id, cmd);
        return false;
    }

    button->last_cmd = cmd;
    button->last_event_tick = now_ticks;
    return true;
}

static esp_err_t press_pentatonic_chord(int button_id)
{
    button_chord_map_t *button = get_button_map(button_id);
    if (button == NULL) {
        ESP_LOGW(TAG, "Unhandled button id %d", button_id);
        return ESP_ERR_NOT_FOUND;
    }

    if (button->is_held) {
        return ESP_OK;
    }

    fill_chord_notes(button, button->active_notes);
    button->is_held = true;
    button->hold_started_tick = xTaskGetTickCount();
    ESP_ERROR_CHECK(update_master_volume_for_density());
    ESP_LOGI(TAG, "Button %d pressed", button_id);

    for (size_t voice = 0; voice < 3; ++voice) {
        ESP_ERROR_CHECK(amy_esp_adf_note_on(button->synth, button->active_notes[voice], HELD_CHORD_VELOCITIES[voice], 0));
    }

    return ESP_OK;
}

static esp_err_t release_pentatonic_chord(int button_id)
{
    button_chord_map_t *button = get_button_map(button_id);
    if (button == NULL) {
        ESP_LOGW(TAG, "Unhandled button id %d", button_id);
        return ESP_ERR_NOT_FOUND;
    }

    if (!button->is_held) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Button %d released", button_id);
    for (size_t voice = 0; voice < 3; ++voice) {
        ESP_ERROR_CHECK(amy_esp_adf_note_off(button->synth, button->active_notes[voice], 0));
    }

    button->is_held = false;
    button->hold_started_tick = 0;
    ESP_ERROR_CHECK(amy_esp_adf_set_synth_pan(button->synth, BUTTON_PANS[(size_t)(button - BUTTON_MAP)]));
    ESP_ERROR_CHECK(update_master_volume_for_density());

    return ESP_OK;
}

esp_err_t app_synth_init(void)
{
    amy_esp_adf_config_t amy_cfg = amy_esp_adf_default_config();
    amy_cfg.master_volume = MASTER_VOLUME_TARGETS[0];
    ESP_ERROR_CHECK(amy_esp_adf_init(&amy_cfg));

    for (size_t i = 0; i < sizeof(BUTTON_MAP) / sizeof(BUTTON_MAP[0]); ++i) {
        button_chord_map_t *button = &BUTTON_MAP[i];
        button->is_held = false;
        button->hold_started_tick = 0;
        button->last_cmd = -1;
        button->last_event_tick = 0;
        for (size_t voice = 0; voice < 3; ++voice) {
            button->active_notes[voice] = 0;
        }
        ESP_ERROR_CHECK(amy_esp_adf_configure_synth(button->synth, 1024, 3));
        ESP_ERROR_CHECK(amy_esp_adf_set_synth_pan(button->synth, BUTTON_PANS[i]));
    }

    ESP_ERROR_CHECK(amy_esp_adf_set_fx(1.90f, 0.42f, 0.12f));

    TickType_t now_ticks = xTaskGetTickCount();
    s_state.next_melody_pulse = app_beat_now_pulse();
    s_state.next_chord_motion_tick = now_ticks;
    s_state.next_filter_tick = now_ticks;
    s_state.melody_step = 0;
    s_state.phrase_index = 0;
    s_state.phrase_counter = 0;
    s_state.filter_phase = 0.0f;
    ESP_ERROR_CHECK(update_melody_filter_motion(now_ticks));

    return ESP_OK;
}

audio_element_handle_t app_synth_create_stream(int task_core, int task_prio)
{
    return amy_esp_adf_stream_init_for_core("amy", task_core, task_prio);
}

int app_synth_sample_rate(void)
{
    return amy_esp_adf_sample_rate();
}

int app_synth_channels(void)
{
    return amy_esp_adf_channels();
}

bool app_synth_handle_button_event(const audio_event_iface_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    if (msg->source_type != PERIPH_ID_BUTTON) {
        return false;
    }

    int button_id = (int)(intptr_t)msg->data;
    button_chord_map_t *button = get_button_map(button_id);
    if (button == NULL) {
        ESP_LOGW(TAG, "Unhandled button id %d", button_id);
        return false;
    }

    if (!should_accept_button_event(button, msg->cmd, xTaskGetTickCount())) {
        return false;
    }

    if (msg->cmd == PERIPH_BUTTON_PRESSED) {
        return press_pentatonic_chord(button_id) == ESP_OK;
    }

    if (msg->cmd == PERIPH_BUTTON_RELEASE || msg->cmd == PERIPH_BUTTON_LONG_RELEASE) {
        return release_pentatonic_chord(button_id) == ESP_OK;
    }

    return false;
}

void app_synth_update(uint32_t now_ticks)
{
    size_t phrase_len = sizeof(MELODY_PHRASES[0]) / sizeof(MELODY_PHRASES[0][0]);
    uint64_t now_pulse = app_beat_now_pulse();

    ESP_ERROR_CHECK(update_chord_motion((TickType_t)now_ticks));
    ESP_ERROR_CHECK(update_melody_filter_motion((TickType_t)now_ticks));

    while (now_pulse >= s_state.next_melody_pulse) {
        uint8_t midi_note = melody_note_for_step(s_state.phrase_index, s_state.melody_step);
        ESP_ERROR_CHECK(amy_esp_adf_schedule_note(MELODY_SYNTH, midi_note, MELODY_VELOCITY, MELODY_NOTE_DURATION_MS, 10));

        s_state.melody_step = (s_state.melody_step + 1) % phrase_len;
        if (s_state.melody_step == 0) {
            s_state.phrase_counter++;
            if ((s_state.phrase_counter % 2) == 0) {
                s_state.phrase_index = (s_state.phrase_index + 1) % (sizeof(MELODY_PHRASES) / sizeof(MELODY_PHRASES[0]));
            } else if ((esp_random() % 4) == 0) {
                s_state.phrase_index = (s_state.phrase_index + 1) % (sizeof(MELODY_PHRASES) / sizeof(MELODY_PHRASES[0]));
            }
        }

        s_state.next_melody_pulse += MELODY_STEP_PULSES;
    }
}
