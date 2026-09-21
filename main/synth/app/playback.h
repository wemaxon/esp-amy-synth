#ifndef SYNTH_APP_PLAYBACK_H
#define SYNTH_APP_PLAYBACK_H

#include "synth/app/state.h"

/* Application-task scheduling; no additional state or task ownership. */
void synth_app_playback_stop_strum(synth_app_state_t *app);
void synth_app_playback_start_strum(synth_app_state_t *app, uint8_t slot, const synth_chord_t *chord);
void synth_app_playback_update_strum(synth_app_state_t *app);
void synth_app_playback_stop_arp(synth_app_state_t *app);
void synth_app_playback_update_arp(synth_app_state_t *app);
void synth_app_playback_stop_repeat(synth_app_state_t *app);
void synth_app_playback_update_repeat(synth_app_state_t *app);

#endif
