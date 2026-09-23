#include "synth/amy_perf.h"

#include <string.h>

#include "amy.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pocket_audio_board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "synth/looper.h"

typedef struct {
    uint32_t blocks;
    uint32_t slow_blocks;
    uint32_t cycle_over_budget_blocks;
    uint64_t render_us_total;
    uint64_t render_us_max;
    uint64_t amy_us_total;
    uint64_t amy_us_max;
    uint64_t flanger_us_total;
    uint64_t flanger_us_max;
    uint64_t looper_us_total;
    uint64_t looper_us_max;
    uint64_t write_us_total;
    uint64_t write_us_max;
    uint64_t input_us_total;
    uint64_t input_us_max;
    uint32_t input_errors;
    uint64_t cycle_us_total;
    uint64_t cycle_us_max;
    int64_t next_log_us;
    int64_t next_profile_log_us;
} amy_perf_stats_t;

#if ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED
typedef struct {
    uint16_t audible;
    uint16_t audible_low;
    uint16_t audible_high;
    uint16_t aux;
    uint16_t suspended;
} amy_osc_stats_t;
#endif

#if ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED || ESP_POCKET_AUDIO_AMY_PROFILE_LOG_ENABLED
static const char *TAG = "SYNTH_AMY";
#endif

static amy_perf_stats_t s_perf;

#if ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED
static amy_osc_stats_t amy_runtime_osc_stats(void)
{
    amy_osc_stats_t stats = {0};
    if (synth == NULL) {
        return stats;
    }

    uint16_t split = (uint16_t)(AMY_OSCS / 2U);
    for (uint16_t osc = 0; osc < AMY_OSCS; ++osc) {
        if (synth[osc] == NULL) {
            continue;
        }

        if (synth[osc]->role == SYNTH_IS_MOD_SOURCE ||
            synth[osc]->role == SYNTH_IS_ALGO_SOURCE) {
            ++stats.aux;
            continue;
        }

        switch (synth[osc]->status) {
        case SYNTH_AUDIBLE:
            ++stats.audible;
            if (osc < split) {
                ++stats.audible_low;
            } else {
                ++stats.audible_high;
            }
            break;
        case SYNTH_INAUDIBLE:
            ++stats.suspended;
            break;
        default:
            break;
        }
    }
    return stats;
}
#endif

void synth_amy_perf_reset(void)
{
    memset(&s_perf, 0, sizeof(s_perf));
}

void synth_amy_perf_record_block(uint32_t render_us,
                                 uint32_t amy_us,
                                 uint32_t flanger_us,
                                 uint32_t looper_us,
                                 uint32_t write_us,
                                 uint32_t input_us,
                                 bool input_error,
                                 int64_t now_us,
                                 uint16_t app_active_oscs,
                                 uint16_t allocated_patch_oscs,
                                 uint32_t audio_block_budget_us)
{
#if ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED || ESP_POCKET_AUDIO_AMY_PROFILE_LOG_ENABLED
#if ESP_POCKET_AUDIO_AMY_PERF_LOG_ENABLED
    if (s_perf.next_log_us == 0) {
        s_perf.next_log_us =
            now_us + ((int64_t)ESP_POCKET_AUDIO_AMY_PERF_LOG_INTERVAL_MS * 1000);
    }

    ++s_perf.blocks;
    uint32_t cycle_us = render_us + write_us;
    s_perf.render_us_total += render_us;
    s_perf.amy_us_total += amy_us;
    s_perf.flanger_us_total += flanger_us;
    s_perf.looper_us_total += looper_us;
    s_perf.write_us_total += write_us;
    s_perf.input_us_total += input_us;
    s_perf.cycle_us_total += cycle_us;
    if (render_us > s_perf.render_us_max) {
        s_perf.render_us_max = render_us;
    }
    if (amy_us > s_perf.amy_us_max) {
        s_perf.amy_us_max = amy_us;
    }
    if (flanger_us > s_perf.flanger_us_max) {
        s_perf.flanger_us_max = flanger_us;
    }
    if (looper_us > s_perf.looper_us_max) {
        s_perf.looper_us_max = looper_us;
    }
    if (write_us > s_perf.write_us_max) {
        s_perf.write_us_max = write_us;
    }
    if (input_us > s_perf.input_us_max) {
        s_perf.input_us_max = input_us;
    }
    if (input_error) {
        ++s_perf.input_errors;
    }
    if (cycle_us > s_perf.cycle_us_max) {
        s_perf.cycle_us_max = cycle_us;
    }
    if (render_us > audio_block_budget_us) {
        ++s_perf.slow_blocks;
    }
    if (cycle_us > audio_block_budget_us) {
        ++s_perf.cycle_over_budget_blocks;
    }

    if (now_us >= s_perf.next_log_us && s_perf.blocks > 0) {
        uint32_t render_avg = (uint32_t)(s_perf.render_us_total / s_perf.blocks);
        uint32_t amy_avg = (uint32_t)(s_perf.amy_us_total / s_perf.blocks);
        uint32_t flanger_avg = (uint32_t)(s_perf.flanger_us_total / s_perf.blocks);
        uint32_t looper_avg = (uint32_t)(s_perf.looper_us_total / s_perf.blocks);
        uint32_t write_avg = (uint32_t)(s_perf.write_us_total / s_perf.blocks);
        uint32_t input_avg = (uint32_t)(s_perf.input_us_total / s_perf.blocks);
        uint32_t cycle_avg = (uint32_t)(s_perf.cycle_us_total / s_perf.blocks);
        float render_cpu_pct = audio_block_budget_us > 0 ?
            ((float)render_avg * 100.0f) / (float)audio_block_budget_us :
            0.0f;
        uint32_t free_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        uint32_t min_heap = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        uint32_t largest_heap = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        uint32_t internal_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        UBaseType_t stack_words = uxTaskGetStackHighWaterMark(NULL);
        amy_osc_stats_t amy_oscs = amy_runtime_osc_stats();
        synth_looper_diagnostics_t looper = {0};
        synth_looper_get_diagnostics(&looper);

        esp_log_level_t level = s_perf.slow_blocks > 0 ? ESP_LOG_WARN : ESP_LOG_INFO;
        ESP_LOG_LEVEL(level,
                      TAG,
                      "perf blocks=%u app_oscs=%u amy_audible_oscs=%u low=%u high=%u aux=%u suspended=%u alloc_oscs=%u render_us avg=%u max=%u budget=%u cpu_est=%.1f%% slow=%u amy_us avg=%u max=%u flanger_us avg=%u max=%u mixer_us avg=%u max=%u i2s_us avg=%u max=%u input_us avg=%u max=%u errors=%u cycle_us avg=%u max=%u over=%u looper_queue depth=%u high=%u rejected=%u latency_blocks=%u snapshot_retries=%u snapshot_skipped=%u looper_lifetime_max_us=%u looper_over_budget=%u heap=%u min=%u largest=%u internal=%u audio_stack_free_words=%u",
                      (unsigned)s_perf.blocks,
                      (unsigned)app_active_oscs,
                      (unsigned)amy_oscs.audible,
                      (unsigned)amy_oscs.audible_low,
                      (unsigned)amy_oscs.audible_high,
                      (unsigned)amy_oscs.aux,
                      (unsigned)amy_oscs.suspended,
                      (unsigned)allocated_patch_oscs,
                      (unsigned)render_avg,
                      (unsigned)s_perf.render_us_max,
                      (unsigned)audio_block_budget_us,
                      render_cpu_pct,
                      (unsigned)s_perf.slow_blocks,
                      (unsigned)amy_avg,
                      (unsigned)s_perf.amy_us_max,
                      (unsigned)flanger_avg,
                      (unsigned)s_perf.flanger_us_max,
                      (unsigned)looper_avg,
                      (unsigned)s_perf.looper_us_max,
                      (unsigned)write_avg,
                      (unsigned)s_perf.write_us_max,
                      (unsigned)input_avg,
                      (unsigned)s_perf.input_us_max,
                      (unsigned)s_perf.input_errors,
                      (unsigned)cycle_avg,
                      (unsigned)s_perf.cycle_us_max,
                      (unsigned)s_perf.cycle_over_budget_blocks,
                      (unsigned)looper.command_depth,
                      (unsigned)looper.command_queue_high_water,
                      (unsigned)looper.command_rejected,
                      (unsigned)looper.max_command_latency_blocks,
                      (unsigned)looper.snapshot_reader_retries,
                      (unsigned)looper.snapshot_publish_skipped,
                      (unsigned)looper.max_process_us,
                      (unsigned)looper.process_over_budget,
                      (unsigned)free_heap,
                      (unsigned)min_heap,
                      (unsigned)largest_heap,
                      (unsigned)internal_heap,
                      (unsigned)stack_words);

        s_perf.blocks = 0;
        s_perf.slow_blocks = 0;
        s_perf.cycle_over_budget_blocks = 0;
        s_perf.render_us_total = 0;
        s_perf.render_us_max = 0;
        s_perf.amy_us_total = 0;
        s_perf.amy_us_max = 0;
        s_perf.flanger_us_total = 0;
        s_perf.flanger_us_max = 0;
        s_perf.looper_us_total = 0;
        s_perf.looper_us_max = 0;
        s_perf.write_us_total = 0;
        s_perf.write_us_max = 0;
        s_perf.input_us_total = 0;
        s_perf.input_us_max = 0;
        s_perf.input_errors = 0;
        s_perf.cycle_us_total = 0;
        s_perf.cycle_us_max = 0;
        s_perf.next_log_us =
            now_us + ((int64_t)ESP_POCKET_AUDIO_AMY_PERF_LOG_INTERVAL_MS * 1000);
    }
#else
    (void)render_us;
    (void)amy_us;
    (void)flanger_us;
    (void)looper_us;
    (void)write_us;
    (void)input_us;
    (void)input_error;
    (void)app_active_oscs;
    (void)allocated_patch_oscs;
    (void)audio_block_budget_us;
#endif

#if ESP_POCKET_AUDIO_AMY_PROFILE_LOG_ENABLED
    if (s_perf.next_profile_log_us == 0) {
        s_perf.next_profile_log_us =
            now_us + ((int64_t)ESP_POCKET_AUDIO_AMY_PROFILE_LOG_INTERVAL_MS * 1000);
    }
    if (now_us >= s_perf.next_profile_log_us) {
        ESP_LOGI(TAG, "AMY profile window follows");
        amy_profiles_print();
        s_perf.next_profile_log_us =
            now_us + ((int64_t)ESP_POCKET_AUDIO_AMY_PROFILE_LOG_INTERVAL_MS * 1000);
    }
#else
    (void)now_us;
#endif
#else
    (void)render_us;
    (void)amy_us;
    (void)flanger_us;
    (void)looper_us;
    (void)write_us;
    (void)input_us;
    (void)input_error;
    (void)now_us;
    (void)app_active_oscs;
    (void)allocated_patch_oscs;
    (void)audio_block_budget_us;
#endif
}
