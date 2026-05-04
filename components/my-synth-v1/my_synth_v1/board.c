#include "board.h"

#include "audio_mem.h"
#include "esp_log.h"
#include "led_indicator.h"

static const char *TAG = "MY_SYNTH_V1_BOARD";

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void)
{
    if (board_handle) {
        ESP_LOGW(TAG, "The board has already been initialized");
        return board_handle;
    }

    board_handle = (audio_board_handle_t)audio_calloc(1, sizeof(struct audio_board_handle));
    AUDIO_MEM_CHECK(TAG, board_handle, return NULL);

    board_handle->audio_hal = audio_board_codec_init();

    return board_handle;
}

audio_hal_handle_t audio_board_codec_init(void)
{
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
    audio_hal_handle_t codec_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    AUDIO_NULL_CHECK(TAG, codec_hal, return NULL);
    return codec_hal;
}

display_service_handle_t audio_board_led_init(void)
{
    led_indicator_handle_t led = led_indicator_init((gpio_num_t)get_green_led_gpio());
    display_service_config_t display = {
        .based_cfg = {
            .task_stack = 0,
            .task_prio = 0,
            .task_core = 0,
            .task_func = NULL,
            .service_start = NULL,
            .service_stop = NULL,
            .service_destroy = NULL,
            .service_ioctl = led_indicator_pattern,
            .service_name = "DISPLAY_serv",
            .user_data = NULL,
        },
        .instance = led,
    };

    return display_service_create(&display);
}

esp_err_t audio_board_key_init(esp_periph_set_handle_t set)
{
    (void)set;
    return my_synth_v1_input_init();
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_deinit(audio_board_handle_t audio_board)
{
    esp_err_t ret = ESP_OK;
    if (audio_board == NULL) {
        return ESP_OK;
    }

    if (audio_board->audio_hal != NULL) {
        ret |= audio_hal_deinit(audio_board->audio_hal);
    }

    audio_free(audio_board);
    board_handle = NULL;
    return ret;
}
