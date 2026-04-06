#include "app_synth.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "periph_button.h"
#include "board.h"
#include "amy_esp_adf.h"

static const char *TAG = "app_synth";

static const uint8_t MELODY_SYNTH = 1;
static const uint16_t MELODY_NOTE_DURATION_MS = 320;
static const uint16_t MELODY_STEP_MS = 500;
static const uint8_t PENTATONIC_BASE_NOTE = 60;
static const uint8_t PENTATONIC_INTERVALS[] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21};
static const float HELD_CHORD_VELOCITIES[] = {0.44f, 0.34f, 0.26f};
static const float MELODY_VELOCITY = 0.86f;
static const float MASTER_VOLUME_TARGETS[] = {2.45f, 2.18f, 1.95f, 1.78f};
static const TickType_t BUTTON_DEBOUNCE_TICKS = pdMS_TO_TICKS(40);

typedef struct {
    int button_id;
    size_t scale_degree;
    uint8_t synth;
    bool is_held;
    uint8_t active_notes[3];
    int last_cmd;
    TickType_t last_event_tick;
} button_chord_map_t;

typedef struct {
    uint32_t next_melody_tick;
    size_t melody_step;
    size_t phrase_index;
    uint32_t phrase_counter;
} app_synth_state_t;

static app_synth_state_t s_state = {
    .next_melody_tick = 0,
    .melody_step = 0,
    .phrase_index = 0,
    .phrase_counter = 0,
};

static button_chord_map_t BUTTON_MAP[] = {
    {BUTTON_REC_ID, 0, 20, false, {0}, -1, 0},
    {BUTTON_MODE_ID, 1, 21, false, {0}, -1, 0},
    {BUTTON_SET_ID, 2, 22, false, {0}, -1, 0},
    {BUTTON_PLAY_ID, 3, 23, false, {0}, -1, 0},
    {BUTTON_VOLDOWN_ID, 4, 24, false, {0}, -1, 0},
    {BUTTON_VOLUP_ID, 5, 25, false, {0}, -1, 0},
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
        button->last_cmd = -1;
        button->last_event_tick = 0;
        for (size_t voice = 0; voice < 3; ++voice) {
            button->active_notes[voice] = 0;
        }
        ESP_ERROR_CHECK(amy_esp_adf_configure_synth(button->synth, 1024, 3));
    }

    ESP_ERROR_CHECK(amy_esp_adf_set_fx(1.45f, 0.26f, 0.14f));

    s_state.next_melody_tick = xTaskGetTickCount();
    s_state.melody_step = 0;
    s_state.phrase_index = 0;
    s_state.phrase_counter = 0;

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

    while ((int32_t)(now_ticks - s_state.next_melody_tick) >= 0) {
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

        s_state.next_melody_tick += pdMS_TO_TICKS(MELODY_STEP_MS);
    }
}
