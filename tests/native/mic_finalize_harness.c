#include "test_check.h"
#include "synth/app/mic.h"

static synth_mic_sample_snapshot_t current;
static esp_err_t analyze_result, install_result;
static unsigned sequence, install_count;
static float installed_note;
static const int16_t sample[] = {100, -100};

esp_err_t synth_mic_sample_analyze(void)
{
    sequence = sequence * 10 + 1;
    return analyze_result;
}
void synth_mic_sample_snapshot(synth_mic_sample_snapshot_t *snapshot)
{
    sequence = sequence * 10 + 2;
    *snapshot = current;
}
const int16_t *synth_mic_sample_data(uint32_t *frames)
{
    *frames = 2;
    return sample;
}
esp_err_t synth_amy_install_mic_sample(const int16_t *samples, uint32_t frames, float note)
{
    sequence = sequence * 10 + 3;
    ++install_count;
    installed_note = note;
    return samples == sample && frames == 2 ? install_result : ESP_FAIL;
}
void synth_mic_sample_mark_installed(bool installed)
{
    sequence = sequence * 10 + 4;
    current.state = installed ? SYNTH_MIC_SAMPLE_READY : SYNTH_MIC_SAMPLE_ERROR;
}
static int finalize(bool pitched, esp_err_t analyze, esp_err_t install)
{
    sequence = install_count = 0;
    current = (synth_mic_sample_snapshot_t){.pitch = {.pitched = pitched, .midi_note = 69.0f}};
    analyze_result = analyze;
    install_result = install;
    synth_mic_sample_snapshot_t snapshot;
    esp_err_t err = synth_app_mic_finalize(&snapshot);
    if (analyze != ESP_OK)
        return err == analyze && !install_count && sequence == 1242 && snapshot.state == SYNTH_MIC_SAMPLE_ERROR;
    return err == install && install_count == 1 && sequence == 12342 &&
           installed_note == (pitched ? 69.0f : 60.0f) &&
           snapshot.state == (install == ESP_OK ? SYNTH_MIC_SAMPLE_READY : SYNTH_MIC_SAMPLE_ERROR);
}
int32_t host_vector_mic_finalize_pitched(void) { return finalize(true, ESP_OK, ESP_OK); }
int32_t host_vector_mic_finalize_raw(void) { return finalize(false, ESP_OK, ESP_OK); }
int32_t host_vector_mic_finalize_analysis_error(void) { return finalize(true, ESP_FAIL, ESP_OK); }
int32_t host_vector_mic_finalize_install_error(void) { return finalize(true, ESP_OK, ESP_ERR_NO_MEM); }

int main(void)
{
    CHECK_VECTOR(host_vector_mic_finalize_pitched, 1);
    CHECK_VECTOR(host_vector_mic_finalize_raw, 1);
    CHECK_VECTOR(host_vector_mic_finalize_analysis_error, 1);
    CHECK_VECTOR(host_vector_mic_finalize_install_error, 1);
    return 0;
}
