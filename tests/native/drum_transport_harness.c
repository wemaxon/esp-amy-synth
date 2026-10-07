#include "test_check.h"
#include <stdint.h>

#include "synth/app/drum_transport.h"

#define RATE 44100U
/* 120 BPM sixteenth = 5512.5 frames: steps fall on floor(n * 5512.5). */

/* Without a loop the beat's grid starts now. */
int32_t host_vector_beat_starts_now(void)
{
    synth_app_drum_transport_t t = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER;
    synth_app_drum_transport_start(&t, SYNTH_DRUM_LOOP_FUNK, 120, RATE, 50000, NULL);
    synth_beat_config_t config;
    synth_app_drum_transport_config(&t, &config);
    return config.running && config.origin_frame == 50000 && config.bpm == 120 &&
           config.style == SYNTH_DRUM_LOOP_FUNK;
}

/* Over an existing loop the beat sits on the loop's grid. */
int32_t host_vector_beat_locks_to_loop(void)
{
    synth_app_drum_transport_t t = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER;
    const uint64_t anchor = 1000000;
    synth_app_drum_transport_start(&t, SYNTH_DRUM_LOOP_ROCK, 120, RATE, anchor + 13781, &anchor);
    /* An anchor in the future (not yet reached) falls back to now. */
    synth_app_drum_transport_t later = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER;
    const uint64_t future = 99;
    synth_app_drum_transport_start(&later, SYNTH_DRUM_LOOP_ROCK, 120, RATE, 50, &future);
    return t.origin_frame == anchor && later.origin_frame == 50;
}

/* Each start is a new restart for the scheduler; edits are not. */
int32_t host_vector_restart_ids(void)
{
    synth_app_drum_transport_t t = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER;
    synth_app_drum_transport_start(&t, SYNTH_DRUM_LOOP_ROCK, 120, RATE, 0, NULL);
    uint32_t first = t.restart_id;
    t.variation = SYNTH_DRUM_LOOP_VARIATION_FILLS;
    synth_app_drum_transport_update(&t, 120, RATE, 1000);
    if (t.restart_id != first) return 0;
    synth_app_drum_transport_start(&t, SYNTH_DRUM_LOOP_DISCO, 120, RATE, 2000, NULL);
    synth_app_drum_transport_stop(&t);
    synth_beat_config_t config;
    synth_app_drum_transport_config(&t, &config);
    return t.restart_id != first && !config.running;
}

/* A tempo change keeps the next due step where it is (no jump in time). */
int32_t host_vector_tempo_change_keeps_next_step(void)
{
    synth_app_drum_transport_t t = SYNTH_APP_DRUM_TRANSPORT_INITIALIZER;
    const uint64_t origin = 1000000;
    synth_app_drum_transport_start(&t, SYNTH_DRUM_LOOP_ROCK, 120, RATE, origin, NULL);
    /* At origin + 13781 the next step is 3, due at origin + 16537. */
    if (!synth_app_drum_transport_update(&t, 60, RATE, origin + 13781)) return 0;
    /* At 60 BPM step 3 is 33075 frames from the new origin. */
    if (t.origin_frame + 33075 != origin + 16537) return 0;
    return !synth_app_drum_transport_update(&t, 60, RATE, origin + 20000);
}

int main(void)
{
    CHECK_VECTOR(host_vector_beat_starts_now, 1);
    CHECK_VECTOR(host_vector_beat_locks_to_loop, 1);
    CHECK_VECTOR(host_vector_restart_ids, 1);
    CHECK_VECTOR(host_vector_tempo_change_keeps_next_step, 1);
    return 0;
}
