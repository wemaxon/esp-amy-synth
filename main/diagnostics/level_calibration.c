#include "diagnostics/level_calibration.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "amy.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi/oled_display.h"
#include "pocket_oled.h"
#include "synth/amy_engine.h"
#include "synth/audio_meter.h"
#include "synth/drums.h"
#include "synth/level_calibration.h"
#include "synth/sound.h"

#if ESP_POCKET_AUDIO_LEVEL_CALIBRATION_ENABLED

static const char *TAG = "LEVEL_CAL";

static const uint8_t MELODIC_NOTES[] = {48U, 60U, 72U};
static const uint8_t CHORD_NOTES[] = {48U, 60U, 64U, 67U};
static const uint8_t STRESS_NOTES[] = {48U, 55U, 60U, 64U, 67U, 72U};
static const uint32_t MELODIC_WINDOW_FRAMES = AMY_SAMPLE_RATE * 5U;
static const uint32_t MELODIC_HOLD_FRAMES =
    ((AMY_SAMPLE_RATE * 4U) / AMY_BLOCK_SIZE) * AMY_BLOCK_SIZE;
static const uint32_t DRUM_WINDOW_FRAMES = AMY_SAMPLE_RATE * 2U;
static const uint8_t REPEAT_COUNT = 2U;
static const uint8_t CALIBRATED_SOUND_COUNT = SYNTH_SOUND_MIC_SAMPLE;

typedef enum {
    CALIBRATION_TRIGGER_MELODIC = 0,
    CALIBRATION_TRIGGER_DRUM,
} calibration_trigger_kind_t;

typedef struct {
    calibration_trigger_kind_t kind;
    synth_chord_t chord;
    synth_drum_kit_t kit;
    synth_drum_voice_t voice;
    esp_err_t start_result;
    esp_err_t stop_result;
} calibration_trigger_t;

/* Exclusive calibration has exactly one outstanding fixture at a time. */
static calibration_trigger_t s_trigger;

typedef struct {
    uint32_t session_id;
    uint32_t completed;
    uint32_t total;
    const esp_pocket_audio_hw_t *hw;
} calibration_run_t;

static float power_db(float value)
{
    return value > 0.0f ? 10.0f * log10f(value) : -200.0f;
}

static float amplitude_db(float value)
{
    return value > 0.0f ? 20.0f * log10f(value) : -200.0f;
}

static void draw_progress(const calibration_run_t *run,
                          const char *category,
                          const char *name,
                          const char *fixture)
{
    if (run == NULL || run->hw == NULL || !pocket_oled_is_initialized(&run->hw->display)) {
        return;
    }

    pocket_oled_t *display = (pocket_oled_t *)&run->hw->display;
    char line[32];
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "LEVEL CALIBRATION");
    snprintf(line, sizeof(line), "%lu / %lu",
             (unsigned long)run->completed,
             (unsigned long)run->total);
    pocket_oled_draw_text_row(display, 2, line);
    pocket_oled_draw_text_row(display, 3, category);
    pocket_oled_draw_text_row(display, 4, name);
    pocket_oled_draw_text_row(display, 5, fixture);
    pocket_oled_draw_text_row(display, 7, "SPEAKER MUTED");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        oled_display_flush_cooperatively(display, pdMS_TO_TICKS(25))
    );
}

static void draw_complete(const calibration_run_t *run, esp_err_t result)
{
    if (run == NULL || run->hw == NULL || !pocket_oled_is_initialized(&run->hw->display)) {
        return;
    }

    pocket_oled_t *display = (pocket_oled_t *)&run->hw->display;
    pocket_oled_clear(display);
    pocket_oled_draw_text_row(display, 0, "LEVEL CALIBRATION");
    pocket_oled_draw_text_row(display, 3, result == ESP_OK ? "COMPLETE" : "FAILED");
    pocket_oled_draw_text_row(display, 5, "COPY SERIAL JSONL");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        oled_display_flush_cooperatively(display, pdMS_TO_TICKS(25))
    );
}

static esp_err_t wait_for_session(uint32_t session_id,
                                  bool complete,
                                  uint32_t timeout_ms,
                                  synth_audio_meter_result_t *result)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    do {
        synth_audio_meter_result_t snapshot = {0};
        if (synth_audio_meter_read(&snapshot) && snapshot.session_id == session_id &&
            (complete ? snapshot.complete : snapshot.active)) {
            if (result != NULL) {
                *result = snapshot;
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((int32_t)(deadline - xTaskGetTickCount()) > 0);
    return ESP_ERR_TIMEOUT;
}

static void start_fixture_at_block_boundary(void *context)
{
    calibration_trigger_t *trigger = context;
    if (trigger->kind == CALIBRATION_TRIGGER_MELODIC) {
        trigger->start_result = synth_amy_chord_on(0, &trigger->chord);
    } else {
        trigger->start_result = synth_drums_hit(trigger->kit, trigger->voice, 100U);
    }
}

static void stop_fixture_at_block_boundary(void *context)
{
    calibration_trigger_t *trigger = context;
    trigger->stop_result = synth_amy_chord_off(0);
}

static esp_err_t start_meter(calibration_run_t *run,
                             synth_audio_meter_tap_t tap,
                             uint32_t target_frames,
                             uint32_t stop_after_frames,
                             synth_audio_meter_boundary_hook_t on_start,
                             synth_audio_meter_boundary_hook_t on_stop,
                             void *hook_context)
{
    synth_audio_meter_request_t request = {
        .session_id = ++run->session_id,
        .tap = tap,
        .target_frames = target_frames,
        .stop_after_frames = stop_after_frames,
        .on_start = on_start,
        .on_stop = on_stop,
        .hook_context = hook_context,
    };
    return synth_audio_meter_start(&request);
}

static void print_measurement(const calibration_run_t *run,
                              const char *category,
                              const char *fixture,
                              uint8_t repeat,
                              int sound,
                              int note,
                              int kit,
                              int voice,
                              float applied_gain,
                              const synth_audio_meter_result_t *result)
{
    printf("{\"schema\":\"esp-pocket-audio.level.v1\","
           "\"type\":\"measurement\",\"session\":%lu,"
           "\"category\":\"%s\",\"fixture\":\"%s\",\"repeat\":%u,"
           "\"sound\":%d,\"note\":%d,\"kit\":%d,\"voice\":%d,"
           "\"applied_gain\":%.9g,\"tap\":\"%s\","
           "\"frames\":%llu,\"peak_dbfs\":%.4f,\"rms_dbfs\":%.4f,"
           "\"lufs\":%.4f,\"momentary_lufs\":%.4f,"
           "\"exposure_lufs\":%.4f,\"clipped_samples\":%llu,"
           "\"meter_max_us\":%lu,\"meter_deadline_misses\":%lu}\n",
           (unsigned long)run->session_id,
           category,
           fixture,
           (unsigned)repeat,
           sound,
           note,
           kit,
           voice,
           applied_gain,
           result->tap == SYNTH_AUDIO_METER_TAP_DRY ? "dry" : "output",
           (unsigned long long)result->measured_frames,
           amplitude_db(result->sample_peak),
           power_db(result->mean_square),
           -0.691f + power_db(result->k_weighted_mean_square),
           -0.691f + power_db(result->max_momentary_k_mean_square),
           -0.691f + power_db(result->k_weighted_exposure),
           (unsigned long long)result->clipped_samples,
           (unsigned long)result->process_time_max_us,
           (unsigned long)result->process_deadline_misses);
    fflush(stdout);
}

static synth_chord_t make_chord(const uint8_t *notes, uint8_t note_count)
{
    synth_chord_t chord = {0};
    chord.note_count = note_count;
    for (uint8_t index = 0; index < note_count; ++index) {
        chord.notes[index] = notes[index];
    }
    chord.root_midi = notes[0];
    return chord;
}

static esp_err_t configure_clean_measurement_state(bool stereo_enabled)
{
    ESP_RETURN_ON_ERROR(synth_amy_reset_exclusive(), TAG, "AMY reset failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_volume(1.0f), TAG, "measurement volume setup failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_reverb_enabled(false), TAG, "reverb disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_chorus_enabled(false), TAG, "chorus disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_delay_enabled(false, 0.0f), TAG, "delay disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_tremolo_enabled(false), TAG, "tremolo disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_flanger_enabled(false), TAG, "flanger disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_lfo_enabled(false), TAG, "LFO disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_filter_enabled(false), TAG, "filter disable failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_stereo_enabled(stereo_enabled),
                        TAG,
                        "stereo setup failed");
    return ESP_OK;
}

static esp_err_t measure_melodic(calibration_run_t *run,
                                 synth_sound_t sound,
                                 const char *fixture,
                                 const uint8_t *notes,
                                 uint8_t note_count,
                                 synth_audio_meter_tap_t tap,
                                 bool stereo_enabled,
                                 uint8_t repeat)
{
    draw_progress(run, "MELODIC", synth_sound_name(sound), fixture);
    ESP_RETURN_ON_ERROR(configure_clean_measurement_state(stereo_enabled),
                        TAG,
                        "clean measurement state failed");
    ESP_RETURN_ON_ERROR(synth_amy_set_sound(sound), TAG, "sound setup failed");
    vTaskDelay(pdMS_TO_TICKS(150));
    s_trigger = (calibration_trigger_t){
        .kind = CALIBRATION_TRIGGER_MELODIC,
        .chord = make_chord(notes, note_count),
        .start_result = ESP_ERR_INVALID_STATE,
        .stop_result = ESP_ERR_INVALID_STATE,
    };
    ESP_RETURN_ON_ERROR(start_meter(run,
                                    tap,
                                    MELODIC_WINDOW_FRAMES,
                                    MELODIC_HOLD_FRAMES,
                                    start_fixture_at_block_boundary,
                                    stop_fixture_at_block_boundary,
                                    &s_trigger),
                        TAG,
                        "meter request rejected");

    synth_audio_meter_result_t result = {0};
    ESP_RETURN_ON_ERROR(wait_for_session(run->session_id, true, 6500U, &result),
                        TAG,
                        "meter completion timed out");
    ESP_RETURN_ON_ERROR(s_trigger.start_result, TAG, "fixture note on failed");
    ESP_RETURN_ON_ERROR(s_trigger.stop_result, TAG, "fixture note off failed");
    print_measurement(run,
                      "melodic",
                      fixture,
                      repeat,
                      sound,
                      note_count == 1U ? notes[0] : -1,
                      -1,
                      -1,
                      synth_level_calibration_sound_gain(sound),
                      &result);
    ++run->completed;
    return ESP_OK;
}

static esp_err_t measure_drum(calibration_run_t *run,
                              synth_drum_kit_t kit,
                              synth_drum_voice_t voice,
                              uint8_t repeat)
{
    draw_progress(run, "DRUM", synth_drum_kit_name(kit), synth_drum_voice_name(voice));
    ESP_RETURN_ON_ERROR(configure_clean_measurement_state(false),
                        TAG,
                        "clean measurement state failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    s_trigger = (calibration_trigger_t){
        .kind = CALIBRATION_TRIGGER_DRUM,
        .kit = kit,
        .voice = voice,
        .start_result = ESP_ERR_INVALID_STATE,
        .stop_result = ESP_OK,
    };
    ESP_RETURN_ON_ERROR(start_meter(run,
                                    SYNTH_AUDIO_METER_TAP_DRY,
                                    DRUM_WINDOW_FRAMES,
                                    0U,
                                    start_fixture_at_block_boundary,
                                    NULL,
                                    &s_trigger),
                        TAG,
                        "meter request rejected");

    synth_audio_meter_result_t result = {0};
    ESP_RETURN_ON_ERROR(wait_for_session(run->session_id, true, 3500U, &result),
                        TAG,
                        "meter completion timed out");
    ESP_RETURN_ON_ERROR(s_trigger.start_result, TAG, "drum hit failed");
    print_measurement(run,
                      "drum",
                      "hit_v100",
                      repeat,
                      -1,
                      -1,
                      kit,
                      voice,
                      synth_level_calibration_drum_gain(kit, voice),
                      &result);
    ++run->completed;
    return ESP_OK;
}

static esp_err_t run_fixtures(calibration_run_t *run)
{
    for (uint8_t repeat = 0; repeat < REPEAT_COUNT; ++repeat) {
        for (synth_sound_t sound = SYNTH_SOUND_SAW;
             sound < CALIBRATED_SOUND_COUNT;
             sound = (synth_sound_t)(sound + 1)) {
            for (uint8_t note = 0; note < sizeof(MELODIC_NOTES); ++note) {
                char fixture[16];
                snprintf(fixture, sizeof(fixture), "note_%u", (unsigned)MELODIC_NOTES[note]);
                ESP_RETURN_ON_ERROR(measure_melodic(run,
                                                    sound,
                                                    fixture,
                                                    &MELODIC_NOTES[note],
                                                    1U,
                                                    SYNTH_AUDIO_METER_TAP_DRY,
                                                    false,
                                                    repeat),
                                    TAG,
                                    "note fixture failed");
            }
            ESP_RETURN_ON_ERROR(measure_melodic(run,
                                                sound,
                                                "chord_c",
                                                CHORD_NOTES,
                                                sizeof(CHORD_NOTES),
                                                SYNTH_AUDIO_METER_TAP_DRY,
                                                false,
                                                repeat),
                                TAG,
                                "chord fixture failed");
            ESP_RETURN_ON_ERROR(measure_melodic(run,
                                                sound,
                                                "poly_stress",
                                                STRESS_NOTES,
                                                sizeof(STRESS_NOTES),
                                                SYNTH_AUDIO_METER_TAP_OUTPUT,
                                                true,
                                                repeat),
                                TAG,
                                "stress fixture failed");
        }
    }

    for (uint8_t repeat = 0; repeat < REPEAT_COUNT; ++repeat) {
        for (synth_drum_kit_t kit = SYNTH_DRUM_KIT_TIGHT;
             kit < SYNTH_DRUM_KIT_COUNT;
             kit = (synth_drum_kit_t)(kit + 1)) {
            for (synth_drum_voice_t voice = SYNTH_DRUM_VOICE_KICK;
                 voice < SYNTH_DRUM_VOICE_COUNT;
                 voice = (synth_drum_voice_t)(voice + 1)) {
                ESP_RETURN_ON_ERROR(measure_drum(run, kit, voice, repeat),
                                    TAG,
                                    "drum fixture failed");
            }
        }
    }
    return ESP_OK;
}

esp_err_t diagnostics_level_calibration_run(const esp_pocket_audio_hw_t *hw)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    calibration_run_t run = {
        .total = (uint32_t)REPEAT_COUNT *
                 ((uint32_t)CALIBRATED_SOUND_COUNT * 5U +
                  (uint32_t)SYNTH_DRUM_KIT_COUNT * SYNTH_DRUM_VOICE_COUNT),
        .hw = hw,
    };
    bool restore_amp = esp_pocket_audio_amp_is_enabled();
    if (restore_amp) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pocket_audio_amp_set_enabled(false));
    }

    ESP_LOGI(TAG,
             "starting exclusive calibration fixtures=%lu revision=%s",
             (unsigned long)run.total,
             synth_level_calibration_revision());
    printf("{\"schema\":\"esp-pocket-audio.level.v1\",\"type\":\"run_begin\","
           "\"sample_rate_hz\":%u,\"channels\":%u,\"repeats\":%u,"
           "\"master_volume\":1.0,"
           "\"calibration_revision\":\"%s\"}\n",
           AMY_SAMPLE_RATE,
           AMY_NCHANS,
           REPEAT_COUNT,
           synth_level_calibration_revision());
    fflush(stdout);

    esp_err_t result = synth_amy_init(hw);
    bool synth_initialized = result == ESP_OK;
    if (result == ESP_OK) {
        result = run_fixtures(&run);
    }
    if (synth_initialized) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_all_off());
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            synth_amy_set_volume((float)ESP_POCKET_AUDIO_WHEEL_DEFAULT_PERCENT / 100.0f)
        );
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_sound(SYNTH_SOUND_SAW));
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_filter_enabled(true));
        ESP_ERROR_CHECK_WITHOUT_ABORT(synth_amy_set_stereo_enabled(true));
    }
    if (restore_amp) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pocket_audio_amp_set_enabled(true));
    }

    printf("{\"schema\":\"esp-pocket-audio.level.v1\",\"type\":\"run_end\","
           "\"completed\":%lu,\"expected\":%lu,\"result\":%ld}\n",
           (unsigned long)run.completed,
           (unsigned long)run.total,
           (long)result);
    fflush(stdout);
    draw_complete(&run, result);
    return result;
}

#else

esp_err_t diagnostics_level_calibration_run(const esp_pocket_audio_hw_t *hw)
{
    return hw == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

#endif
