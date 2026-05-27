#include "synth/app_play_state.h"

#include <string.h>

void synth_app_play_stop_strum(synth_app_play_state_t *play)
{
    if (play == NULL) {
        return;
    }
    play->strum_active = false;
    play->strum_slot = SYNTH_APP_PLAY_NO_SLOT;
    play->strum_voice_index = 0;
}

void synth_app_play_start_strum(synth_app_play_state_t *play,
                                uint8_t slot,
                                const synth_chord_t *chord,
                                TickType_t now)
{
    if (play == NULL || chord == NULL) {
        return;
    }
    play->strum_chord = *chord;
    play->strum_slot = slot;
    play->strum_voice_index = 0;
    play->strum_next_tick = now;
    play->strum_active = true;
}

bool synth_app_play_arp_clock_active(const synth_app_play_state_t *play)
{
    return play != NULL && play->arp_last_slot != SYNTH_APP_PLAY_NO_SLOT;
}

void synth_app_play_queue_arp_source(synth_app_play_state_t *play,
                                     uint8_t slot,
                                     const synth_chord_t *chord,
                                     TickType_t now)
{
    if (play == NULL || slot >= SYNTH_CHORD_BUTTON_COUNT || chord == NULL) {
        return;
    }

    bool start_now = !synth_app_play_arp_clock_active(play);
    play->arp_chords[slot] = *chord;
    play->arp_chord_valid[slot] = true;
    if (start_now) {
        play->arp_step_index = 0;
        play->arp_next_tick = now;
    }
}

void synth_app_play_clear_arp_source(synth_app_play_state_t *play, uint8_t slot)
{
    if (play != NULL && slot < SYNTH_CHORD_BUTTON_COUNT) {
        play->arp_chord_valid[slot] = false;
    }
}

void synth_app_play_stop_arp(synth_app_play_state_t *play)
{
    if (play == NULL) {
        return;
    }
    play->arp_last_slot = SYNTH_APP_PLAY_NO_SLOT;
    play->arp_step_index = 0;
}

void synth_app_play_clear_arp_sources(synth_app_play_state_t *play, TickType_t now)
{
    if (play == NULL) {
        return;
    }
    memset(play->arp_chord_valid, 0, sizeof(play->arp_chord_valid));
    play->arp_step_index = 0;
    play->arp_next_tick = now;
}

bool synth_app_play_repeat_clock_active(const synth_app_play_state_t *play)
{
    return play != NULL &&
           (play->repeat_active ||
            play->repeat_gate_open ||
            play->repeat_slot != SYNTH_APP_PLAY_NO_SLOT);
}

void synth_app_play_queue_repeat_source(synth_app_play_state_t *play,
                                        uint8_t slot,
                                        TickType_t now)
{
    if (play == NULL || slot >= SYNTH_CHORD_BUTTON_COUNT) {
        return;
    }
    if (synth_app_play_repeat_clock_active(play)) {
        return;
    }

    play->repeat_slot = slot;
    play->repeat_active = true;
    play->repeat_gate_open = false;
    play->repeat_next_tick = now;
}

void synth_app_play_stop_repeat(synth_app_play_state_t *play)
{
    if (play == NULL) {
        return;
    }
    play->repeat_active = false;
    play->repeat_gate_open = false;
    play->repeat_slot = SYNTH_APP_PLAY_NO_SLOT;
}
