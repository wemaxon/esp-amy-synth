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
static const uint8_t CHORD_SYNTH = 3;
static const uint16_t MELODY_NOTE_DURATION_MS = 320;
static const uint16_t CHORD_NOTE_DURATION_MS = 1200;
static const uint16_t MELODY_STEP_MS = 500;
static const uint8_t PENTATONIC_BASE_NOTE = 60;
static const uint8_t PENTATONIC_INTERVALS[] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21};

typedef struct {
    int button_id;
    size_t scale_degree;
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

static const button_chord_map_t BUTTON_MAP[] = {
    {BUTTON_REC_ID, 0},
    {BUTTON_MODE_ID, 1},
    {BUTTON_SET_ID, 2},
    {BUTTON_PLAY_ID, 3},
    {BUTTON_VOLDOWN_ID, 4},
    {BUTTON_VOLUP_ID, 5},
};

// Phrase variants all stay inside the same pentatonic pool so changes remain musical.
static const uint8_t MELODY_PHRASES[][8] = {
    {0, 2, 4, 2, 0, 2, 4, 7},
    {0, 2, 4, 7, 4, 2, 0, 2},
    {0, 2, 4, 2, 4, 7, 4, 2},
    {0, 2, 4, 7, 9, 7, 4, 2},
};

static bool get_button_scale_degree(int button_id, size_t *scale_degree)
{
    for (size_t i = 0; i < sizeof(BUTTON_MAP) / sizeof(BUTTON_MAP[0]); ++i) {
        if (BUTTON_MAP[i].button_id == button_id) {
            *scale_degree = BUTTON_MAP[i].scale_degree;
            return true;
        }
    }
    return false;
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

static esp_err_t schedule_pentatonic_chord(int button_id)
{
    static const float chord_velocities[] = {0.88f, 0.76f, 0.64f};

    size_t degree = 0;
    if (!get_button_scale_degree(button_id, &degree)) {
        ESP_LOGW(TAG, "Unhandled button id %d", button_id);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t voice = 0; voice < 3; ++voice) {
        uint8_t midi_note = PENTATONIC_BASE_NOTE + PENTATONIC_INTERVALS[degree + (voice * 2)];
        ESP_ERROR_CHECK(amy_esp_adf_schedule_note(
            CHORD_SYNTH,
            midi_note,
            chord_velocities[voice],
            CHORD_NOTE_DURATION_MS,
            0
        ));
    }

    return ESP_OK;
}

esp_err_t app_synth_init(void)
{
    amy_esp_adf_config_t amy_cfg = amy_esp_adf_default_config();
    ESP_ERROR_CHECK(amy_esp_adf_init(&amy_cfg));

    // Melody keeps the richer default synth while chords use a cheap sine patch.
    ESP_ERROR_CHECK(amy_esp_adf_configure_synth(CHORD_SYNTH, 1024, 8));
    ESP_ERROR_CHECK(amy_esp_adf_set_fx(1.25f, 0.24f, 0.16f));

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

    if (msg->source_type != PERIPH_ID_BUTTON || msg->cmd != PERIPH_BUTTON_PRESSED) {
        return false;
    }

    int button_id = (int)(intptr_t)msg->data;
    return schedule_pentatonic_chord(button_id) == ESP_OK;
}

void app_synth_update(uint32_t now_ticks)
{
    size_t phrase_len = sizeof(MELODY_PHRASES[0]) / sizeof(MELODY_PHRASES[0][0]);

    while ((int32_t)(now_ticks - s_state.next_melody_tick) >= 0) {
        uint8_t midi_note = melody_note_for_step(s_state.phrase_index, s_state.melody_step);
        ESP_ERROR_CHECK(amy_esp_adf_schedule_note(MELODY_SYNTH, midi_note, 0.92f, MELODY_NOTE_DURATION_MS, 10));

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
