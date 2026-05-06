# HiChord Feature Specification

This document summarizes the behavior described in
`docs/schematics/pocket-audio-hichord-manual.pdf` and translates it into
implementation guidance for future agents working on this firmware.

The goal is to build a HiChord-like handheld chord workstation: seven musical
buttons, a joystick, three menu buttons, a display, a volume/filter knob, audio
output, optional MIDI output, rhythmic modes, sound design controls, randomizers,
and a simple looper.

## Product Model

HiChord is a portable harmonic sketchpad. The player chooses a key, then uses
seven chord buttons to trigger the diatonic chords of that key. The buttons are
based on the Nashville Number System, so the physical chord layout remains the
same while the selected key transposes the actual notes.

The main design principle is that a beginner should be able to press any chord
button and get musically compatible chords, while advanced users can modify
quality, inversion, octave, rhythm, timbre, and effects in real time.

## Hardware Controls

Required controls:

- 7 chord buttons.
- 3 menu/action buttons.
- 1 joystick with up, down, left, right, and click.
- 1 analog knob used as master volume by default and filter cutoff when filter
  editing is active.
- OLED or equivalent display for current mode, key, tempo, selected effect, and
  settings feedback.
- Stereo 3.5 mm audio output.
- USB-C power and USB MIDI/firmware update support.
- Optional built-in speaker.

The manual uses icon-only names for the three menu buttons, and those icons are
not preserved in the extracted PDF text. In code and UI docs, assign semantic
names instead:

- `KEY_MENU`: key, global octave, and per-chord octave operations.
- `TEMPO_MENU`: BPM and rhythmic-rate operations.
- `SOUND_MENU`: waveform, mode, effect, MIDI, and randomization operations.

If the actual hardware silkscreen is later known, map these semantic names to
the real buttons in one board-specific header.

## Global State

Maintain a single application state object with at least:

- Current key/root.
- Global octave offset.
- BPM.
- Active mode: `PLAY`, `ARP`, `REPEAT`, `DRUM`, `AUTODRUM`.
- Selected chord button, if any is being held.
- Per-chord inversion index.
- Per-chord octave offset.
- Current chord quality modifier from the joystick.
- Sound patch: waveform(s), oscillator count, envelopes, filter, effects, bass
  layer, stereo/mono state.
- Rhythm rate for arpeggio/repeat/autodrum modes.
- Arpeggio pattern.
- MIDI output enabled/disabled.
- Looper state and recorded events.
- Randomizer seed or last randomization metadata, if useful for debugging.

Persist user settings in NVS when practical. Factory reset must restore all
states to defaults.

## Chord Button Mapping

The seven chord buttons always represent scale degrees 1 through 7 in the
current major key:

| Button | Scale Degree | Function | Default Quality |
| ------ | ------------ | -------- | --------------- |
| 1 | I | Tonic | Major |
| 2 | ii | Supertonic | Minor |
| 3 | iii | Mediant | Minor |
| 4 | IV | Subdominant | Major |
| 5 | V | Dominant | Major |
| 6 | vi | Submediant | Minor |
| 7 | vii | Leading tone | Diminished |

Implementation requirements:

- Pressing a chord button triggers the chord for that degree in the selected
  key.
- Chords must transpose instantly when the global key changes.
- Notes should be generated from theory, not hard-coded per key.
- Default mode plays the full chord immediately.
- Chord button events should feed both audio synthesis and MIDI output when MIDI
  is enabled.

## Chord Quality Joystick

When a chord button is held, joystick direction modifies chord quality in real
time. Support these qualities:

- Major/minor toggle or default diatonic quality.
- Dominant/minor seventh.
- Major/minor seventh.
- Major/minor ninth.
- Major sixth.
- Suspended 2.
- Suspended 4.
- Diminished.
- Augmented.

Recommended implementation:

- Represent chord quality as interval sets relative to the chord root.
- Keep diatonic defaults as the fallback when the joystick is centered.
- Apply the modifier while the chord is held.
- Update currently sounding voices without audible stuck notes.

## Chord Inversions

Chord inversions rearrange chord notes for smoother movement.

Required behavior:

- Holding a chord button and pressing the inversion command cycles through
  available inversions for that chord.
- Each chord button has its own inversion index.
- Inversions should wrap around.
- Inversion state should affect play, arp, repeat, and MIDI chord output.

Suggested model:

- Triads: root position, first inversion, second inversion.
- Seventh/ninth chords: include additional inversions if practical, or at least
  rotate the lowest note upward through the chord tones.

## Key And Octave Controls

Global key:

- `KEY_MENU` + joystick left/right changes the key.
- The change instantly transposes all seven chord buttons.
- Display the selected key.

Global octave:

- `KEY_MENU` + joystick up/down raises or lowers the overall octave.
- Clamp to a range that prevents unusably low/high notes on the target synth.

Per-chord octave:

- Holding a chord button plus the per-chord octave down/up commands adjusts only
  that chord button's octave.
- Store a separate octave offset for each of the seven buttons.

## Tempo And Rhythm Rates

BPM:

- `TEMPO_MENU` + joystick left/right decreases/increases BPM.
- BPM drives arp, repeat, autodrum, delays/tremolo rates where applicable, and
  recorded loop playback.
- Start with 120 BPM unless another project default is established.

Rhythm rates:

- Support at least `1/4`, `1/8`, `1/16`, `1/32`, swing variants, and triplet
  variants.
- The same rate system should be shared by arp, repeat, autodrum, delay, and
  tremolo where musically useful.

## Sound Engine

The instrument should support multiple oscillator-based patches per chord.

Required waveforms/sounds:

- Saw.
- Sine.
- Square.
- Saw-square blend.
- Triangle.
- Wurlitzer-like electric piano tone.
- Piano-like tone.
- Randomized generated tone.

Sound behavior:

- `SOUND_MENU` + joystick up cycles waveform/sound.
- Each chord can use 2 oscillators per voice.
- Target up to 10 oscillators per chord when feasible.
- Effects and filters shape the same active patch.
- Randomized sound generation should change oscillator settings, envelope, and
  effect-related parameters in a bounded musical range.

For this repo, prefer AMY-native oscillator, patch, filter, delay, and envelope
features rather than duplicating DSP code in application logic.

## Effects

`SOUND_MENU` opens the sounds/effects selection. Joystick left/right selects the
effect slot shown on the display. Joystick down toggles or cycles the selected
effect's value.

Implement these effect/state slots:

| Slot | Values |
| ---- | ------ |
| ADSR | `LONG`, `SHORT`, `SWELL`, `PLUCK`, `TOUCH`, `SUSTAIN` |
| LFO | `LOW`, `MED`, `HIGH`, `OFF` |
| Glide | `ON`, `OFF` |
| Reverb | `ON`, `OFF` |
| Tremolo | `1/4`, `1/8`, `1/16`, `1/16T`, `1/32`, `OFF` |
| Flanger | `ON`, `OFF` |
| Filter | `ON`, `OFF` |
| Delay | `1/4`, `1/8`, `1/16`, `1/16T`, `1/32`, `OFF` |
| Stereo | `STEREO`, `MONO` |
| Bass | `ON`, `OFF` |
| Voices | `1 OSC`, `2 OSC`, `4 OSC`, `8 OSC` |
| MIDI | `ON`, `OFF` |

Effect notes:

- Filter on/off controls whether the knob edits cutoff. When filter is off, the
  knob is master volume.
- Bass adds a sine sub-oscillator one octave below the chord root.
- Voices controls oscillator density: mono/lead, light layer, thicker chord,
  lush stack.
- Glide should smooth pitch transitions between chord changes.
- Effects must be safe to change while notes are sounding.

## Modes

`SOUND_MENU` + joystick up cycles modes. The display must show the active mode.

### PLAY

Default mode. Pressing a chord button plays the full chord immediately.

Implementation requirements:

- Button down triggers chord on.
- Button up releases chord according to the active ADSR.
- Multiple chord buttons may either replace the current chord or layer chords;
  choose one behavior and document it in code. Replacement is simpler for a
  handheld chord instrument.

### ARP

Arpeggiates the currently held chord by playing its notes one at a time.

Implementation requirements:

- Uses current BPM and arp rhythm rate.
- Uses current chord quality, inversion, octave, and waveform/effects.
- `TEMPO_MENU` + joystick up cycles arp rhythm rates.
- A random pattern command generates a new arpeggio pattern by changing note
  order, timing, and placement.
- MIDI output sends individual arpeggiated notes in sequence.

### REPEAT

Repeats the full chord at a rhythmic rate.

Implementation requirements:

- Uses current BPM and repeat rhythm rate.
- Each repeat retriggers the chord envelope.
- `TEMPO_MENU` + joystick up cycles repeat rates.
- MIDI output sends repeated chord triggers.

### DRUM

Turns the seven chord buttons into drum pads.

Mapping:

| Button | Drum |
| ------ | ---- |
| 1 | Kick |
| 2 | Snare |
| 3 | Closed hi-hat |
| 4 | Open hi-hat |
| 5 | High tom |
| 6 | Mid tom |
| 7 | Crash cymbal |

Implementation requirements:

- Drum mode ignores harmonic key for button sound selection.
- Drum hits should still be recordable by the looper.
- MIDI output should send drum/percussion note messages if enabled.

### AUTODRUM

Automatically plays drum rhythms in sync with the current BPM.

Implementation requirements:

- Uses rhythm rate and swing/triplet timing.
- Plays kick, snare, tom, crash, and hi-hat patterns.
- `TEMPO_MENU` + joystick up cycles autodrum rates.
- Autodrum should be able to run while chord modes play if CPU and UI design
  allow it; otherwise document it as a mutually exclusive mode.

## MIDI Output

USB-C MIDI output can be toggled from the effects menu via the `MIDI` slot.

Required behavior:

- In `PLAY`, send the full chord as a group of MIDI note-on/note-off events.
- In `ARP`, send individual notes in the arpeggio sequence.
- In `REPEAT`, send repeated chord triggers at the selected rhythm rate.
- In `DRUM`, send mapped drum notes.
- Prefer stable MIDI channels and note mappings documented in code.
- MIDI must mirror the audio behavior closely enough that a DAW recording can
  reconstruct the performance.

## Looper

The joystick click controls a simple loop recorder when the user is not inside a
menu screen.

State cycle:

1. Idle -> recording: click starts recording.
2. Recording -> playing: click stops recording and starts loop playback.
3. Playing -> idle: click stops playback and clears the loop.

Implementation requirements:

- Record timestamped musical events rather than rendered audio.
- Capture chord, arp, repeat, and drum triggers.
- Playback should follow the tempo used at record time, or optionally time-scale
  to the current BPM if that behavior is explicitly implemented.
- Clear all loop events on the third click.
- Avoid menu clicks accidentally modifying the looper.

## Randomizers

Implement three randomizer actions:

- Randomize sound: oscillator settings, waveform choices, effects, and
  envelopes.
- Randomize pattern: arpeggio note order, timing, and placement.
- Randomize all: key, BPM, inversions, waveforms, effects, and arpeggio
  patterns.

Randomization rules:

- Keep results musical and bounded.
- Avoid settings that produce silence unless silence is an intentional effect.
- Update the display after randomization.
- Make randomization deterministic under a test seed if practical.

## Factory Reset

Holding the documented reset button combination for 5 seconds restores factory
defaults and restarts the device.

Required reset behavior:

- Clear persisted settings.
- Restore default key, octave, BPM, mode, effects, chord mappings, inversions,
  sound patch, MIDI state, and rhythm settings.
- Restart or visibly reinitialize the app to confirm reset.

Suggested defaults:

- Key: C major.
- BPM: 120.
- Mode: `PLAY`.
- Global octave: middle register suitable for chords.
- Effects: off except basic ADSR.
- MIDI: off.
- Filter: off.
- Stereo: stereo.
- Bass: off.
- Voices: modest default such as `2 OSC` or `4 OSC`.

## Firmware Update

The source device enters DFU mode by holding a three-button combination for 5
seconds, then updating over USB-C from a web programmer.

For this project:

- Preserve a reliable bootloader/update path.
- Document the board-specific update button combination once hardware controls
  are finalized.
- Ensure the firmware can recover from bad saved settings via factory reset.

## Display Requirements

The display should provide immediate feedback for:

- Current mode.
- Current key.
- Current BPM.
- Global octave or per-chord octave when edited.
- Selected waveform/sound.
- Selected effect slot and value.
- MIDI on/off.
- Looper state: idle, recording, playing.
- Randomizer confirmation.
- Factory reset confirmation.

Keep display strings short and stable so embedded UI code remains simple.

## Audio And Hardware Targets

Manual specs to match where practical:

- Stereo 3.5 mm output.
- USB-C 5 V power.
- Approximate device class: handheld, around 160 mm x 90 mm x 30 mm.
- Target sample rate: 48 kHz if supported cleanly; this repo currently configures
  the ESP audio codec and AMY stream, so align the actual rate across codec,
  AMY, and I2S.
- Polyphony/oscillator target: up to 8 voices/oscillators in user-facing voice
  settings, with an aspirational maximum of 10 oscillators per chord if CPU
  budget allows.
- Controls: seven chord buttons, three menu buttons, joystick with click,
  volume/filter knob, OLED.

## Implementation Priorities

Recommended build order:

1. Create the app state model and event routing for buttons, joystick, knob, and
   display updates.
2. Implement Nashville chord generation for major keys.
3. Implement `PLAY` mode with chord on/off, inversions, key, octave, and basic
   waveform selection.
4. Add tempo clock and rhythm-rate utilities.
5. Implement `ARP` and `REPEAT`.
6. Add effect state slots and map them to AMY features.
7. Add drum and autodrum modes.
8. Add MIDI mirroring.
9. Add looper.
10. Add randomizers, persistence, factory reset, and update-mode documentation.

## Testing Checklist

Future agents should verify:

- Each key produces correct diatonic chords on all seven buttons.
- Key changes transpose existing chord mappings correctly.
- Inversions rotate notes and persist per chord button.
- Joystick chord-quality modifiers produce expected interval sets.
- BPM changes affect arp, repeat, autodrum, delay, and tremolo timing.
- Mode changes do not leave stuck notes.
- Effects can be toggled while notes are active.
- Filter knob switches cleanly between volume and cutoff behavior.
- MIDI output matches audio notes in play, arp, repeat, and drum modes.
- Looper state cycle works and does not trigger while in menus.
- Randomizers stay within musical bounds.
- Factory reset clears persisted state and restores defaults.
