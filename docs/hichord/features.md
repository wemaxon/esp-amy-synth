# HiChord Feature Specification

This document summarizes the behavior described in the HiChord manual and
translates it into implementation guidance for future agents working on this
firmware. The current reference source is the public HiChord Manual REV 2.8:
https://hichord.shop/pages/manual

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

The manual uses three colored function buttons. In this firmware, assign
semantic names and map them to the actual board GPIOs in the board config:

- `KEY_MENU` / gray: key, global octave, per-chord octave down, attack, battery,
  factory reset, all-randomize, and some arp shortcuts.
- `SOUND_MENU` / yellow: sound selection, effects, release, presets,
  inversions, chord lock, and sound randomization.
- `MODE_MENU` / red: mode selection, BPM, mode-specific parameters, per-chord
  octave up, filter cutoff, tap tempo, and pattern randomization.

If the actual hardware silkscreen is later known, map these semantic names to
the real buttons in one board-specific header.

## Global State

Maintain a single application state object with at least:

- Current key/root.
- Global octave offset.
- BPM.
- Active mode: `PLAY`, `STRUM`, `LEAD`, `DRONE`, `ARP`, `REPEAT`, `DRUM`,
  `DRUM_LOOP`, `AUTO_DRUM`, `MIC_SAMPLE`, `SEQUENCER`, `MIXER`, `CHORD_HIRO`,
  `TUNER`, or `VOCODER`.
- Selected chord button, if any is being held.
- Per-chord inversion index.
- Per-chord octave offset.
- Per-chord chord-lock modifier, if implemented.
- Current chord quality modifier from the joystick.
- Current joystick modifier mode: default, extended, or chromatic.
- Current scale/mode, at least major initially and eventually the manual's 10
  scales.
- Sound patch: waveform(s), oscillator count, envelopes, filter, effects, bass
  layer, stereo/mono state.
- Rhythm rate for arpeggio, repeat, auto-drum, delay, tremolo, and sequencer
  timing.
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

Reference progressions for demo/test mode:

- `1 -> 5 -> 6 -> 4`
- `1 -> 6 -> 4 -> 5`
- `6 -> 4 -> 1 -> 5`
- `2 -> 5 -> 1`
- `1 -> 4 -> 5`

## Chord Quality Joystick

When a chord button is held, joystick direction modifies chord quality in real
time. Releasing the joystick returns to the base diatonic chord unless chord
lock is active.

Default joystick mode:

| Direction | Modifier |
| --------- | -------- |
| Center | Diatonic base chord |
| Up-left | Augmented |
| Up | Major/minor flip |
| Up-right | Dominant 7 |
| Left | Diminished for major chords / minor handling for minor chords |
| Right | Major 7 on major chords / minor 7 on minor chords |
| Down-left | 6th on major chords / sus2 behavior on minor chords |
| Down | Sus4 |
| Down-right | 9th/add9 |

Extended and chromatic joystick modes should be part of the long-term design,
even if the first implementation only supports default mode.

| Mode | Directions / chord types |
| ---- | ------------------------ |
| Extended | flat 5, Dom9, Add9, Add11, Min11, Sus4+7, half-dim7, plus related color chords |
| Chromatic | Min(Maj7), Maj13, 6/9, Maj7#11, Dom13, Dom7b9, Dom7alt, and other outside-key colors |

Recommended implementation:

- Represent chord quality as interval sets relative to the chord root.
- Keep diatonic defaults as the fallback when the joystick is centered.
- Apply temporary modifiers while the chord is held.
- Add chord lock later: chord button + joystick modifier + lock command toggles
  a persistent modifier for that chord button. Locked chords should transpose
  correctly when the global key changes.
- Update currently sounding voices without stuck notes or double-triggered note
  offs.

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
- Support all 12 keys: `C`, `C#`, `D`, `D#`, `E`, `F`, `F#`, `G`, `G#`, `A`,
  `A#`, `B`.
- The change instantly transposes all seven chord buttons.
- Display the selected key.

Global octave:

- `KEY_MENU` + joystick up/down raises or lowers the overall octave.
- Match the manual range when practical: global octave `-1..+2`.

Per-chord octave:

- Holding a chord button plus the per-chord octave down/up commands adjusts only
  that chord button's octave.
- Match the manual range when practical: per-chord octave `-2..+1`.
- Store a separate octave offset for each of the seven buttons.

## Tempo And Rhythm Rates

BPM:

- `MODE_MENU` + joystick up enters the BPM submenu; joystick left/right changes
  BPM.
- Support a manual-compatible BPM range of `40..300`.
- Tapping the mode button three or more times should set tap tempo.
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

Required engines and sound families:

- Analog engine: sine, saw, square, triangle, and blends.
- FM engine: electric pianos, bells, organs, metallic textures.
- Sample engine: piano, strings, vibraphone, harp, cello, and user-uploaded
  samples where storage permits.
- Noise engine: white, pink, filtered resonant, metallic, and hybrid texture
  components.
- Randomized generated tone.

Sound behavior:

- `SOUND_MENU` opens sounds. Pressing it again advances to the next sound;
  joystick left/right browses sounds.
- `SOUND_MENU` + chord button should eventually quick-select seven favorite
  sounds.
- Target a 12-oscillator architecture where feasible: six musical voices plus
  six detuned stereo partners. The current ESP32/AMY implementation may choose
  a smaller default voice count and expose `1`, `2`, `4`, and later `6` or `12`
  oscillator settings as CPU budget allows.
- Effects and filters shape the same active patch.
- Randomized sound generation should change oscillator settings, envelope, and
  effect-related parameters in a bounded musical range.

For this repo, prefer AMY-native oscillator, patch, filter, delay, and envelope
features rather than duplicating DSP code in application logic.

## Effects

`SOUND_MENU` opens the sounds/effects selection. Joystick up enters effects from
the sound browser. In the effects list, joystick left/right selects the effect
slot shown on the display, and joystick up toggles or cycles the selected
effect's value.

Implement these effect/state slots:

| Slot | Values |
| ---- | ------ |
| ADSR | `LONG`, `SHORT`, `SWELL`, `PLUCK`, `TOUCH`, `SUSTAIN` |
| Attack | `1..2000 ms`, preferably via function button + wheel |
| Release | `1..5000 ms`, preferably via function button + wheel |
| Glide | `ON`, `OFF` |
| Reverb | `ON`, `OFF`, plus amount/type if the synth budget allows |
| Delay | `1/4`, `1/8`, `1/16`, `1/16T`, `1/32`, `OFF` |
| Chorus | `ON`, `OFF`, plus depth/rate if available |
| Tremolo | `1/4`, `1/8`, `1/16`, `1/16T`, `1/32`, `OFF` |
| Flanger | `ON`, `OFF` |
| Filter | `ON`, `OFF`, cutoff `20 Hz..20 kHz` |
| Stereo | `STEREO`, `MONO` |
| Speaker | `ON`, `OFF` when the target hardware has a speaker |
| Bass | `OFF`, `ROOT`, `SLASH` |
| Voice leading | `ON`, `OFF` |
| Voices | `1 OSC`, `2 OSC`, `4 OSC`, `6/12 OSC` depending on CPU budget |
| Joystick mode | `DEFAULT`, `EXTENDED`, `CHROMATIC` |
| Scale | `MAJOR`, `NATURAL_MINOR`, `HARMONIC_MINOR`, `MELODIC_MINOR`, `MAJOR_PENTATONIC`, `MINOR_PENTATONIC`, `BLUES`, `DORIAN`, `MIXOLYDIAN`, `LYDIAN` |
| USB mode | `AUDIO`, `MIDI` |
| MIDI | `ON`, `OFF` |
| Vocoder / sampling | Optional Batch-4-inspired features if microphone hardware exists |

Effect notes:

- Filter on/off controls whether the knob edits cutoff. When filter is off, the
  knob is master volume.
- Bass `ROOT` adds a bass note two octaves below the chord root. Bass `SLASH`
  uses one held chord button as the bass degree and another as the chord.
- Voices controls oscillator density: mono/lead, light layer, thicker chord,
  lush stack.
- Voice leading should choose inversions that minimize movement between
  successive chords.
- Glide should smooth pitch transitions between chord changes.
- Effects must be safe to change while notes are sounding.

## Modes

`MODE_MENU` opens the mode menu. Joystick left/right browses modes, pressing the
mode button again closes the menu, and `MODE_MENU` + chord button should
eventually quick-select the first seven common modes.

Target modes from the manual:

| Mode | Behavior |
| ---- | -------- |
| `PLAY` / oneshot | Polyphonic full chords. Button down plays, release fades. Multiple chord buttons may layer. |
| `STRUM` | Notes of the chord roll out one by one, like a guitar. Speeds: slow `120 ms`, medium `80 ms`, fast `40 ms` between notes. |
| `LEAD` | Monophonic root-note mode. New chord button cuts the previous note. |
| `DRONE` | Press once to sustain indefinitely. New chord switches drone with crossfade. Switching modes stops the drone. |
| `ARP` | Chord notes play one at a time, synced to BPM. |
| `REPEAT` | Full chord retriggers at a selected rhythmic rate. |
| `DRUM` | Seven buttons trigger drum hits. |
| `DRUM_LOOP` / beat | Pre-programmed drum patterns that can keep playing while returning to chord modes. |
| `AUTO_DRUM` | In drum mode, hold a button and use joystick directions to auto-trigger at rhythmic rates. |
| `MIC_SAMPLE` | Optional microphone sampling mode if hardware exists. |
| `SEQUENCER` | Step/pattern mode; looper is disabled while sequencer owns looping. |
| `MIXER` | Looper track mute/solo/level management. |
| `CHORD_HIRO` | Guided chord progression learning mode. |
| `TUNER` | Optional microphone/instrument tuner if hardware exists. |
| `VOCODER` | Optional mic-driven synth mode if hardware exists. |

### Arpeggio Details

Arpeggio mode should support:

- Six patterns: `UP`, `DOWN`, `UP_DOWN`, `DOWN_UP`, `RANDOM`, `FINGERPICK`.
- Nine rate values eventually, sharing the same musical rate enum as delay,
  tremolo, repeat, and auto-drum.
- Three chord modes: `ARP_ONLY`, `CHORD_PLUS_ARP`, `RHYTHM_PLUS_ARP`.
- Pattern randomization from the mode menu click/randomize command.
- MIDI output as individual note events in the generated order.

### Drum Details

Drum button mapping:

| Button | Drum |
| ------ | ---- |
| 1 | Kick |
| 2 | Kick alternate |
| 3 | Snare |
| 4 | Closed hi-hat |
| 5 | Tom |
| 6 | Bell / ride |
| 7 | Open hi-hat / cymbal |

Drum kits should eventually be selectable with function button + chord button:

| Button | Kit |
| ------ | --- |
| 1 | Tight Kit |
| 2 | x0x / 808-style |
| 3 | x9x / 909-style |
| 4 | Lynn Kit |
| 5 | KR-78 |
| 6 | Trap Box |
| 7 | Cycle/default |

Drum loops should eventually provide seven styles with eight variations each:
rock, disco, reggae, funk, hip-hop, electro, and jazz. Variations include
original, ghost notes, busy hi-hats, syncopated kicks, fills, half-time,
double-time, and jazz.

Auto-drum joystick rates:

| Direction | Rate |
| --------- | ---- |
| Up | `1/4` |
| Right | `1/8` |
| Down | `1/16` |
| Left | `1/32` |
| Up-right | Swing eighth |
| Down-right | Swing sixteenth |
| Down-left | Sixteenth triplet |
| Up-left | Swing eighth or alternate swing pattern |

## MIDI Output

USB-C MIDI output can be toggled from the effects menu via the `MIDI` slot.
USB audio and USB MIDI should be treated as mutually exclusive USB modes unless
the target stack can prove both are reliable at once.

Required behavior:

- Send on MIDI channel 1 by default.
- Use a fixed default velocity such as 100 unless expressive velocity is later
  available.
- Support up to 12 simultaneous notes to match the manual's voice architecture,
  but degrade gracefully if the local synth voice count is lower.
- In `PLAY`, send the full chord as a group of MIDI note-on/note-off events.
- In `ARP`, send individual notes in the arpeggio sequence.
- In `REPEAT`, send repeated chord triggers at the selected rhythm rate.
- In `DRUM`, send mapped drum notes.
- If TRS MIDI hardware is ever added, treat it as Type A and mirror USB MIDI
  when MIDI is enabled.
- Prefer stable MIDI channels and note mappings documented in code.
- MIDI must mirror the audio behavior closely enough that a DAW recording can
  reconstruct the performance.

## Looper

The joystick click controls the loop recorder when the user is not inside a
menu screen. Target the manual's six independent tracks, each around 20 seconds,
as an aspirational design point. If RAM/flash is too tight, keep the same state
model with fewer tracks or shorter duration.

State cycle:

1. `OFF`: no active looper action.
2. `WAIT`: armed; optional metronome/count-in or bar-length waiting.
3. `REC`: recording the selected track.
4. `LOOP`: playing recorded tracks.
5. Click again from the final state returns to `OFF` or clears/stops according
   to the UX decision documented in code.

Implementation requirements:

- Prefer timestamped musical events for early firmware. Rendered-audio looping
  can be added later if memory allows.
- Capture chord, arp, repeat, and drum triggers.
- Support left/right track selection. In drum-oriented modes, up/down may be
  used for track or variation selection.
- In `WAIT`, left/right should eventually choose bar count: `0` for free length
  or `1..8` fixed bars.
- Joystick down pauses/resumes all tracks.
- Playback should follow the tempo used at record time, or optionally time-scale
  to the current BPM if that behavior is explicitly implemented.
- Avoid menu clicks accidentally modifying the looper.
- Drum loops, sequencer patterns, and drones should be able to auto-bounce into
  the next empty looper track when leaving their mode, if the audio/event model
  can support it cleanly.

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
- USB mode: audio or MIDI.
- Looper state: off, wait, recording, looping, paused.
- Looper selected track and occupied/empty/recording status.
- Battery state if battery sensing exists.
- Randomizer confirmation.
- Factory reset confirmation.

Keep display strings short and stable so embedded UI code remains simple.

## Audio And Hardware Targets

Manual specs to match where practical:

- Stereo 3.5 mm output.
- USB-C 5 V power.
- Approximate device class: handheld, around 160 mm x 90 mm x 30 mm.
- Target sample rate: 48 kHz if supported cleanly. This repo currently
  configures the ESP audio codec and AMY stream separately, so align the actual
  rate across codec, AMY, USB audio, and I2S.
- Audio format target: 16-bit output with higher precision internal processing
  when AMY/CPU budget allows.
- Polyphony/oscillator target: the manual's architecture is 12 oscillators as
  six stereo pairs. For ESP32, expose lower voice settings first and treat 12
  oscillators as aspirational until measured safe under worst-case effects.
- Suggested signal flow: chord logic -> voices -> ADSR -> bass oscillator ->
  filter -> chorus -> flanger -> delay -> reverb -> looper -> output.
- Latency target: keep control-to-audio response under a few milliseconds where
  possible, and never allow OLED/control I2C to block the audio render path.
- Controls: seven chord buttons, three menu buttons, joystick with click,
  volume/filter knob, OLED.

## Implementation Priorities

Recommended build order:

1. Create the app state model and event routing for buttons, joystick, knob, and
   display updates.
2. Implement Nashville chord generation for major keys.
3. Implement `PLAY` mode with chord on/off, key, octave, and basic
   waveform selection.
4. Add joystick default chord modifiers and clean note replacement.
5. Add inversions, per-chord octave, and chord lock.
6. Add tempo clock and shared rhythm-rate utilities.
7. Implement `STRUM`, `LEAD`, `DRONE`, `ARP`, and `REPEAT`.
8. Add effect state slots and map them to AMY features.
9. Add drum pads, drum loops, and auto-drum.
10. Add MIDI mirroring and USB mode selection.
11. Add looper, then mixer/track display.
12. Add randomizers, persistence, factory reset, and update-mode documentation.

## Testing Checklist

Future agents should verify:

- Each key produces correct diatonic chords on all seven buttons.
- Key changes transpose existing chord mappings correctly.
- Inversions rotate notes and persist per chord button.
- Chord locks persist per chord button and transpose correctly with key changes.
- Joystick chord-quality modifiers produce expected interval sets.
- BPM changes affect arp, repeat, auto-drum, delay, tremolo, sequencer, and
  metronome timing.
- Tap tempo converges after three or more taps.
- Mode changes do not leave stuck notes.
- Layered chord behavior in `PLAY` is intentional and documented.
- Effects can be toggled while notes are active.
- Filter knob switches cleanly between volume and cutoff behavior.
- MIDI output matches audio notes in play, arp, repeat, and drum modes.
- USB audio and USB MIDI mode switching does not leave stale descriptors or
  stuck MIDI notes.
- Looper state cycle works, track selection works, and looper does not trigger
  while in menus.
- Randomizers stay within musical bounds.
- Factory reset clears persisted state and restores defaults.
