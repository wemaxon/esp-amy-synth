#include "hil/hil_app.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hil_console.h"
#include "synth/amy_audio.h"

/* Mono (L+R averaged) audio through the audio tap, which is installed only
 * while a capture runs. */
#define SAMPLE_RATE ((uint32_t)ESP_POCKET_AUDIO_I2S_SAMPLE_RATE_HZ)
#define MAX_MS 10000U

typedef enum {
    SOURCE_OUTPUT = 0, /* what goes to the DAC */
    SOURCE_SYNTH_BUS,  /* synth alone: no drums, no loop playback */
    SOURCE_COUNT,
} capture_source_t;

static const char *const SOURCE_NAMES[SOURCE_COUNT] = {
    [SOURCE_OUTPUT] = "output",
    [SOURCE_SYNTH_BUS] = "synth",
};

static int16_t *s_samples;
static SemaphoreHandle_t s_done;
/* Written before the tap is installed, then only by the audio task. */
static capture_source_t s_source;
static uint32_t s_target;
static uint32_t s_count;
static uint64_t s_first_frame;

esp_err_t hil_capture_init(void)
{
    s_samples = heap_caps_malloc(((size_t)MAX_MS * SAMPLE_RATE / 1000U) * sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_done = xSemaphoreCreateBinary();
    return s_samples != NULL && s_done != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void capture_tap(const synth_amy_audio_tap_block_t *block)
{
    const int16_t *audio = s_source == SOURCE_OUTPUT ? block->output : block->synth_bus;
    if (s_count == 0) {
        s_first_frame = block->frame;
    }
    for (uint32_t i = 0; i < block->frames && s_count < s_target; ++i) {
        int32_t sum = 0;
        for (uint8_t c = 0; c < block->channels; ++c) {
            sum += audio[i * block->channels + c];
        }
        s_samples[s_count++] = (int16_t)(sum / block->channels);
    }
    if (s_count >= s_target) {
        synth_amy_audio_set_tap(NULL);
        xSemaphoreGive(s_done);
    }
}

static esp_err_t run_capture(capture_source_t source, uint32_t frames, uint32_t timeout_ms)
{
    s_source = source;
    s_target = frames;
    s_count = 0;
    synth_amy_audio_set_tap(capture_tap);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        synth_amy_audio_set_tap(NULL);
        vTaskDelay(1);              /* let a tap call in flight finish */
        xSemaphoreTake(s_done, 0);  /* and drop a completion it signalled */
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void hil_command_capture(int argc, char **argv)
{
    capture_source_t source = SOURCE_COUNT;
    for (int i = 0; argc == 3 && i < SOURCE_COUNT; ++i) {
        if (strcmp(argv[1], SOURCE_NAMES[i]) == 0) {
            source = (capture_source_t)i;
        }
    }
    uint32_t ms = argc == 3 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0;
    if (source == SOURCE_COUNT || ms == 0 || ms > MAX_MS) {
        hil_reply_error("usage: capture <output|synth> <1-10000 ms>");
        return;
    }
    uint32_t frames = (uint32_t)(((uint64_t)ms * SAMPLE_RATE) / 1000U);
    if (run_capture(source, frames, ms + 1000U) != ESP_OK) {
        hil_reply_error("capture timed out");
        return;
    }

    hil_send_data(s_samples, (size_t)frames * sizeof(int16_t)); /* int16 little-endian */
    char text[160];
    hil_json_t json;
    hil_json_init(&json, text, sizeof(text));
    hil_json_object(&json, NULL);
    hil_json_string(&json, "capture", SOURCE_NAMES[source]);
    hil_json_uint(&json, "rate", SAMPLE_RATE);
    hil_json_uint(&json, "frames", frames);
    hil_json_uint(&json, "first_frame", s_first_frame);
    hil_json_end(&json);
    hil_reply_json(&json);
}
