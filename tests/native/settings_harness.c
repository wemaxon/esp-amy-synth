#include "test_check.h"
#include <stdint.h>
#include "synth/app/settings.h"

static int same_name(const char *actual, const char *expected)
{
    for (unsigned i = 0; ; ++i) {
        if (actual[i] != expected[i]) return 0;
        if (!expected[i]) return 1;
    }
}

int32_t host_vector_settings_adsr_preset(void)
{
    return same_name(synth_app_adsr_preset_name(0), "LONG") &&
           same_name(synth_app_adsr_preset_name(1), "SHORT") &&
           same_name(synth_app_adsr_preset_name(2), "SWELL") &&
           same_name(synth_app_adsr_preset_name(3), "PLUCK") &&
           same_name(synth_app_adsr_preset_name(4), "TOUCH") &&
           same_name(synth_app_adsr_preset_name(5), "SUSTAIN") &&
           same_name(synth_app_adsr_preset_name(6), "LONG") &&
           same_name(synth_app_adsr_preset_name(255), "LONG");
}

int32_t host_vector_settings_glide_level(void)
{
    return same_name(synth_app_glide_level_name(0), "OFF") &&
           same_name(synth_app_glide_level_name(1), "FAST") &&
           same_name(synth_app_glide_level_name(2), "MED") &&
           same_name(synth_app_glide_level_name(3), "SLOW") &&
           same_name(synth_app_glide_level_name(4), "OFF") &&
           same_name(synth_app_glide_level_name(255), "OFF");
}

int32_t host_vector_settings_delay_level(void)
{
    return same_name(synth_app_delay_level_name(0), "OFF") &&
           same_name(synth_app_delay_level_name(1), "1/4") &&
           same_name(synth_app_delay_level_name(2), "1/8") &&
           same_name(synth_app_delay_level_name(3), "1/16") &&
           same_name(synth_app_delay_level_name(4), "1/16T") &&
           same_name(synth_app_delay_level_name(5), "OFF") &&
           same_name(synth_app_delay_level_name(255), "OFF");
}

int32_t host_vector_settings_tremolo_level(void)
{
    return same_name(synth_app_tremolo_level_name(0), "OFF") &&
           same_name(synth_app_tremolo_level_name(1), "1/4") &&
           same_name(synth_app_tremolo_level_name(2), "1/8") &&
           same_name(synth_app_tremolo_level_name(3), "1/16") &&
           same_name(synth_app_tremolo_level_name(4), "1/16T") &&
           same_name(synth_app_tremolo_level_name(5), "1/32") &&
           same_name(synth_app_tremolo_level_name(6), "OFF") &&
           same_name(synth_app_tremolo_level_name(255), "OFF");
}

int32_t host_vector_settings_voice_level(void)
{
    return same_name(synth_app_voice_level_name(0), "8") &&
           same_name(synth_app_voice_level_name(1), "4") &&
           same_name(synth_app_voice_level_name(2), "2") &&
           same_name(synth_app_voice_level_name(3), "1") &&
           same_name(synth_app_voice_level_name(4), "8") &&
           same_name(synth_app_voice_level_name(255), "8");
}

int main(void)
{
    CHECK_VECTOR(host_vector_settings_adsr_preset, 1);
    CHECK_VECTOR(host_vector_settings_glide_level, 1);
    CHECK_VECTOR(host_vector_settings_delay_level, 1);
    CHECK_VECTOR(host_vector_settings_tremolo_level, 1);
    CHECK_VECTOR(host_vector_settings_voice_level, 1);
    return 0;
}
