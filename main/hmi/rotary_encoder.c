#include "hmi/rotary_encoder.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "ROTARY_ENCODER";
static const int PCNT_LOW_LIMIT = -32767;
static const int PCNT_HIGH_LIMIT = 32767;

static void release_encoder(rotary_encoder_t *encoder, bool unit_enabled)
{
    if (encoder == NULL) {
        return;
    }
    if (unit_enabled && encoder->unit != NULL) {
        pcnt_unit_disable(encoder->unit);
    }
    if (encoder->phase_b_channel != NULL) {
        pcnt_del_channel(encoder->phase_b_channel);
    }
    if (encoder->phase_a_channel != NULL) {
        pcnt_del_channel(encoder->phase_a_channel);
    }
    if (encoder->unit != NULL) {
        pcnt_del_unit(encoder->unit);
    }
    *encoder = (rotary_encoder_t){0};
}

esp_err_t rotary_encoder_init(rotary_encoder_t *encoder,
                              const rotary_encoder_config_t *config)
{
    if (encoder == NULL || config == NULL ||
        encoder->initialized ||
        config->phase_a_gpio < 0 || config->phase_b_gpio < 0 ||
        config->phase_a_gpio == config->phase_b_gpio ||
        config->counts_per_detent == 0 ||
        (config->direction != -1 && config->direction != 1)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    bool unit_enabled = false;
    pcnt_unit_config_t unit_config = {
        .low_limit = PCNT_LOW_LIMIT,
        .high_limit = PCNT_HIGH_LIMIT,
        .flags.accum_count = true,
    };
    ESP_GOTO_ON_ERROR(pcnt_new_unit(&unit_config, &encoder->unit), fail, TAG,
                      "PCNT unit allocation failed");

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = config->glitch_filter_ns,
    };
    ESP_GOTO_ON_ERROR(pcnt_unit_set_glitch_filter(encoder->unit, &filter_config),
                      fail, TAG, "PCNT glitch filter failed");

    gpio_pull_mode_t pull_mode = config->internal_pullups ? GPIO_PULLUP_ONLY : GPIO_FLOATING;
    ESP_GOTO_ON_ERROR(gpio_set_pull_mode(config->phase_a_gpio, pull_mode), fail, TAG,
                      "phase A pull configuration failed");
    ESP_GOTO_ON_ERROR(gpio_set_pull_mode(config->phase_b_gpio, pull_mode), fail, TAG,
                      "phase B pull configuration failed");

    pcnt_chan_config_t channel_config = {
        .edge_gpio_num = config->phase_a_gpio,
        .level_gpio_num = config->phase_b_gpio,
    };
    ESP_GOTO_ON_ERROR(pcnt_new_channel(encoder->unit, &channel_config,
                                       &encoder->phase_a_channel),
                      fail, TAG, "phase A PCNT channel failed");
    channel_config.edge_gpio_num = config->phase_b_gpio;
    channel_config.level_gpio_num = config->phase_a_gpio;
    ESP_GOTO_ON_ERROR(pcnt_new_channel(encoder->unit, &channel_config,
                                       &encoder->phase_b_channel),
                      fail, TAG, "phase B PCNT channel failed");

    ESP_GOTO_ON_ERROR(
        pcnt_channel_set_edge_action(encoder->phase_a_channel,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE),
        fail, TAG, "phase A edge action failed");
    ESP_GOTO_ON_ERROR(
        pcnt_channel_set_level_action(encoder->phase_a_channel,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
        fail, TAG, "phase A level action failed");
    ESP_GOTO_ON_ERROR(
        pcnt_channel_set_edge_action(encoder->phase_b_channel,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE),
        fail, TAG, "phase B edge action failed");
    ESP_GOTO_ON_ERROR(
        pcnt_channel_set_level_action(encoder->phase_b_channel,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
        fail, TAG, "phase B level action failed");

    ESP_GOTO_ON_ERROR(pcnt_unit_add_watch_point(encoder->unit, PCNT_LOW_LIMIT),
                      fail, TAG, "PCNT low-limit watch point failed");
    ESP_GOTO_ON_ERROR(pcnt_unit_add_watch_point(encoder->unit, PCNT_HIGH_LIMIT),
                      fail, TAG, "PCNT high-limit watch point failed");
    ESP_GOTO_ON_ERROR(pcnt_unit_enable(encoder->unit), fail, TAG,
                      "PCNT unit enable failed");
    unit_enabled = true;
    ESP_GOTO_ON_ERROR(pcnt_unit_clear_count(encoder->unit), fail, TAG,
                      "PCNT counter clear failed");
    ESP_GOTO_ON_ERROR(pcnt_unit_start(encoder->unit), fail, TAG,
                      "PCNT unit start failed");

    encoder->counts_per_detent = config->counts_per_detent;
    encoder->direction = config->direction;
    encoder->last_count = 0;
    encoder->position = (rotary_encoder_position_t){0};
    encoder->initialized = true;
    return ESP_OK;

fail:
    release_encoder(encoder, unit_enabled);
    return ret;
}

esp_err_t rotary_encoder_get_position(rotary_encoder_t *encoder,
                                      int32_t *position)
{
    if (encoder == NULL || position == NULL || !encoder->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    int count = 0;
    esp_err_t err = pcnt_unit_get_count(encoder->unit, &count);
    if (err != ESP_OK) {
        return err;
    }
    int32_t delta_counts = count - encoder->last_count;
    encoder->last_count = count;
    *position = rotary_encoder_position_advance(&encoder->position,
                                                delta_counts,
                                                encoder->counts_per_detent,
                                                encoder->direction);
    return ESP_OK;
}
