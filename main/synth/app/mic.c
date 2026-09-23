#include "synth/app/mic.h"

#include "synth/amy_engine.h"

esp_err_t synth_app_mic_finalize(synth_mic_sample_snapshot_t *snapshot)
{
    esp_err_t err = synth_mic_sample_analyze();
    synth_mic_sample_snapshot(snapshot);
    if (err == ESP_OK) {
        uint32_t frames = 0;
        const int16_t *samples = synth_mic_sample_data(&frames);
        float reference_note = snapshot->pitch.pitched ? snapshot->pitch.midi_note : 60.0f;
        err = synth_amy_install_mic_sample(samples, frames, reference_note);
    }
    synth_mic_sample_mark_installed(err == ESP_OK);
    synth_mic_sample_snapshot(snapshot);
    return err;
}
