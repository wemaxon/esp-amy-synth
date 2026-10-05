# HiChord Synth Feature Specification

Rev 2.8 feature inventory. The chord-engine sections (2, 3.3-3.8 and 4) follow
the [Rev 2.9 beta manual](https://hichord.github.io/hichord-beta-updater/manual/)
and companion app, checked 2026-10-05. See [maintenance guidance](../maintenance.md#chords)
for implemented behavior and remaining parity gaps.

Source: HiChord Manual & User Guide, Rev 2.8, https://hichord.shop/pages/manual
Purpose: implementation-focused feature inventory for Codex or another coding agent.

## 0. Implementation Scope

Build an interactive software model of the HiChord synth workflow, including:

- A 7-button chord performance surface.
- Three menu/function buttons: Key/Settings, Sound/Effects, Mode/Tempo.
- An 8-way joystick plus joystick-click action.
- A volume wheel that controls master volume and context-specific parameters.
- A small OLED-style display/state model.
- A polyphonic synth engine with chord generation, voicing, effects, play modes, looper, drums, presets, samples, MIDI/USB abstractions, and companion-app-editable parameters.

The implementation may initially be virtual/software-only, but should preserve the manual's state machine, feature names, ranges, and user interactions.

## 1. Hardware / UI Model

### 1.1 Physical controls

Implement these input controls:

- `chordButtons[1..7]`: seven primary musical buttons.
- `joystick`: supports `up`, `down`, `left`, `right`, `upLeft`, `upRight`, `downLeft`, `downRight`, plus `click`.
- `keyButton` / Gray button: opens Key & Settings menu.
- `soundButton` / Yellow button: opens Sounds & Effects menu.
- `modeButton` / Red button: opens Modes & Tempo menu.
- `volumeWheel`: controls output volume by default; modifies parameters when used with function buttons.
- `powerSwitch`: on/off.

### 1.2 Display model

Implement a 64×32 OLED-style display abstraction with enough states to show:

- Current chord name.
- Current key and scale.
- Current sound/instrument.
- Current mode.
- Current BPM.
- Effect names and effect states.
- Looper track pages and track states.
- Battery percentage/voltage where available.
- Preset slots and save/load marks.
- Tuner detected pitch and tuning offset.
- Chord Hiro scrolling-note game UI.
- Ear Trainer prompt/result UI.
- Calibration prompts and results.
- Error/status messages such as `WAITING`, `REC`, `PLAY`, `LOCKED`, `SYSTEM FULL`, `DONE! SAVED`, `ERROR BAD CAL`.

### 1.3 Ports and hardware capabilities

Represent these device capabilities, even if simulated:

- Built-in mono speaker.
- 3.5 mm stereo headphone/line out; plugging in headphones auto-mutes speaker.
- USB-C charging.
- USB audio: class-compliant 48 kHz stereo.
- USB MIDI: channel 1, up to 12 simultaneous notes, fixed velocity 100.
- TRS MIDI Type A output for Batch 4+.
- Built-in microphone for Batch 4+ only.
- Rechargeable Li-Po battery.

### 1.4 Batch feature flags

Support feature availability by hardware batch:

| Feature | Batch 1 | Batch 2 | Batch 4+ |
|---|---:|---:|---:|
| Core synth, effects, modes | yes | yes | yes |
| 6-track looper | yes | yes | yes |
| USB Audio and USB MIDI | yes | yes | yes |
| Battery level display | no | yes | yes |
| Built-in microphone | no | no | yes |
| Vocoder MIC mode | no | no | yes |
| Tuner | no | no | yes |
| Mic Sample recording | no | no | yes |
| Slash chords | no | no | yes |
| Mixer multi-mute | no | no | yes |
| TRS MIDI output | no | no | yes |

Expose this as configuration, e.g. `device.batch = 1 | 2 | 4`.

## 2. Global State Model

Suggested top-level state fields:

```ts
type HiChordState = {
  power: 'off' | 'on' | 'sleep' | 'deepSleep' | 'dfu' | 'calibration';
  batch: 1 | 2 | 4;
  key: 'C' | 'C#' | 'D' | 'D#' | 'E' | 'F' | 'F#' | 'G' | 'G#' | 'A' | 'A#' | 'B';
  scale: ScaleName;
  globalOctaveShift: -1 | 0 | 1 | 2;
  perButtonOctaveShift: Record<1|2|3|4|5|6|7, -2|-1|0|1|2>;
  bpm: number; // 40..300
  selectedSound: SoundName;
  selectedMode: PlayMode;
  joystickMode: 'DEFAULT' | 'EXTENDED' | 'CHROMATIC' | 'BORROW' | 'OG';
  effects: EffectsState;
  chordLocks: Record<1|2|3|4|5|6|7, { modification: ChordModification; key: Key } | null>;
  inversions: Record<1|2|3|4|5|6|7, 0 | 1 | 2>;
  bassMode: 'OFF' | 'ROOT' | 'SLASH';
  looper: LooperState;
  drums: DrumState;
  sequencer: SequencerState;
  presets: PresetSlot[]; // P1..P4
  samples: SampleState;
  midi: MidiState;
  usbMode: 'AUDIO' | 'MIDI';
  speakerEnabled: boolean;
  battery: BatteryState;
};
```

## 3. Chord Engine

### 3.1 Seven chord buttons

Each chord button maps to a scale degree. In the default key of C Major:

| Button | Degree | Default chord quality | Default chord in C | Musical role |
|---:|---|---|---|---|
| 1 | I | Major | C | Home / tonic |
| 2 | ii | Minor | Dm | Gentle forward motion |
| 3 | iii | Minor | Em | Darker alternative to tonic |
| 4 | IV | Major | F | Lift / openness |
| 5 | V | Major | G | Tension / dominant |
| 6 | vi | Minor | Am | Emotional / relative minor |
| 7 | vii° | Diminished | Bdim | Suspense / leading-tone pull |

Acceptance criteria:

- Pressing a chord button plays a full chord in the current key and scale.
- Any sequence of the seven buttons should be diatonic to the selected key/scale.
- Display shows the chord name.
- Multiple chord buttons can layer chords in Play mode.

### 3.2 Keys

Implement all 12 chromatic keys:

`C, C#, D, D#, E, F, F#, G, G#, A, A#, B`

Controls:

- Open key menu with Gray/Key button.
- Joystick left/right changes key.
- In Chromatic joystick mode, when no chord is held, joystick left/right shifts the entire key down/up by one semitone.

### 3.3 Scales

Implement 10 selectable scales. Each scale changes which notes/chords the 7 chord buttons produce.

| Scale | Character | Suggested use |
|---|---|---|
| Major | Bright, happy; default | Pop, rock, country |
| Natural Minor | Dark, melancholy | Ballads, emo, metal |
| Harmonic Minor | Dramatic | Classical, flamenco |
| Melodic Minor | Sophisticated, jazzy | Jazz, film score |
| Major Pentatonic | Simple, singable | Folk, country, gospel |
| Minor Pentatonic | Gritty, raw | Blues, rock, hip-hop |
| Blues | Adds blue note flavor | Blues, funk, soul |
| Dorian | Minor with bright 6th | Funk, jazz fusion |
| Mixolydian | Major with flat 7th | Rock, classic soul |
| Lydian | Dreamy raised 4th | Film score, dreampop |

Controls:

- Scale is a setting in the Sounds/Effects menu, after the effects:
  Voice, ADSR, Bass, Glide, **Scale**, V.Lead, Stereo, Joystick, ...
- Joystick up cycles through the 10 scales.
- Scale resets to Major at power-off, like key, octave and tempo.

Chord construction (manual: "Picking a scale relights the 7 buttons with a
different set of notes"): button N is degree N of the scale in the current key.
For seven-note scales the chord stacks the scale's own third and fifth, e.g.
Harmonic Minor in C gives `Cm Ddim D#aug Fm G G# Bdim`. Joystick modifiers then
apply by chord family (major/minor/diminished), exactly as in Major.

The public material does not define the button chords of Major Pentatonic,
Minor Pentatonic or Blues (five/six notes on seven buttons). Its only hint is
"Min6: Minor Pentatonic scale (button 6)".

### 3.4 Octave shifting

Global octave shift:

- Range: `-2` to `+2` octaves ("from 2 octaves below to 2 above").
- Control: open key menu, joystick up/down.

Per-button octave shift:

- Range: `-2` to `+2` octaves ("down to 2 octaves below ... up to 2 octaves above").
- Control: hold a chord button and use the relevant octave shortcut.

Acceptance criteria:

- Global shift transposes all chord buttons.
- Per-button shift affects only that button.
- Per-button and global shifts combine when calculating final MIDI/audio pitch.

### 3.5 BPM / tempo

BPM controls tempo-synced features:

- Delay.
- Tremolo.
- Arpeggiator.
- Repeat mode.
- Drum loops.
- Sequencer.
- Metronome and count-in.

Range: `40..300 BPM`.

Controls:

- Red/Mode button opens mode menu.
- Joystick up enters BPM menu.
- Joystick left/right adjusts BPM.
- Tap Tempo: tap Red/Mode button three or more times in rhythm.

### 3.6 Chord inversions

Implement three inversion states per chord button:

- `0`: root position.
- `1`: first inversion.
- `2`: second inversion.

Controls:

- Hold chord button.
- Press inversion shortcut; each press cycles `Root -> 1st -> 2nd -> Root`.

Acceptance criteria:

- Inversions reorder the same chord tones so a different note is lowest.
- Inversions affect voice assignment and display/chord rendering.
- Presets save inversion state.

### 3.7 Voice leading

Effect/setting: `Voice Leading` toggle.

Behavior:

- When off, use explicit button inversion state.
- When on, automatically choose the inversion for the next chord that minimizes pitch movement from currently sounding chord tones ("Each note moves the shortest distance to the next chord").
- This applies to chord button playback and should preserve smooth transitions.

### 3.8 Chord Lock

Behavior:

- Normally joystick chord modifications are temporary while the chord button and joystick direction are held.
- Chord Lock stores the current joystick modification on a chord button.
- Locked modification persists when the chord button is pressed later.
- A locked chord stays pinned to the key it was locked in. It does not follow
  later key changes (Rev 2.9; Rev 2.8 transferred locks to the new key).

Controls:

- Hold chord button.
- Push joystick to desired modification.
- While holding both, press lock shortcut.
- Display shows `LOCKED`.
- Repeat same action to unlock.

### 3.9 Bass and slash chords

Effect/setting: `Bass`, cycles `OFF -> ROOT -> SLASH`.

Modes:

- `OFF`: no dedicated bass voice; all six voices are used for chord tones/fill.
- `ROOT`: adds a bass note two octaves below the chord root.
- `SLASH`: Batch 4+; hold one chord button for bass degree, press another for chord; display shows slash chord notation such as `Em/C`.

Acceptance criteria:

- Bass voice maps to voice/oscillator 4 (`osc 3` and paired `osc 9`) in the voice architecture.
- ROOT bass follows the chord root.
- SLASH bass follows the bass button's scale degree while the upper chord follows the chord button.
- SLASH mode unavailable or disabled for batches before Batch 4.

## 4. Joystick Chord Modification System

### 4.1 General behavior

- Holding a chord button and pushing the joystick in any of 8 directions modifies the chord character.
- Releasing the joystick snaps back unless Chord Lock is active.
- Major and minor chords react differently to the same direction (for example
  Right adds Maj7 to major chords but min7 to minor chords).
- There are five joystick modes: `DEFAULT`, `EXTENDED`, `CHROMATIC`, `BORROW`, `OG`.
- Joystick mode is selected in the effects/settings menu under `JOYSTICK`, cycling
  `DEFAULT -> EXTENDED -> CHROMATIC -> BORROW -> OG` (companion-app CC 23 values 0-4).

### 4.2 Default joystick mode

| Direction | Name / result | Theory behavior | Example in C |
|---|---|---|---|
| Up | Major ↔ Minor flip | Flip the 3rd; major becomes minor, minor becomes major | C -> Cm |
| Up-right | Dom7 | Add flat 7th | C -> C7 |
| Right | Maj7 / min7 | Add natural 7th for major chords; minor 7th for minor chords | C -> CMaj7; Am -> Am7 |
| Down-right | 9th | A full 9th chord: the 7th plus the 9th. On the diminished chord it plays m7b5 | C -> Cmaj9; Dm -> Dm9; Bdim -> Bm7b5 |
| Down | Sus4 | Replace 3rd with 4th | C -> Csus4 |
| Down-left | 6th / Sus2 | Major chords add 6th; minor chords become sus2 | C -> C6; Dm -> Dsus2 |
| Left | Dim / Min | Major and minor chords become diminished; the diminished chord (button 7) becomes minor | C -> Cdim; Bdim -> Bm |
| Up-left | Aug | Raise 5th | C -> Caug |

Rev 2.8 played Down-right as add9-style color; Rev 2.9 plays true 9ths (see OG).

### 4.3 Extended joystick mode

Extended mode provides richer jazz/R&B voicings.

| Direction | Result | Chord change | Vibe |
|---|---|---|---|
| Up | Major ↔ Minor | Flip 3rd | Bright/dark flip |
| Down | Dom7#9 | Add flat 7th and sharp 9th | Hendrix crunch |
| Left | Sus4+7 | Replace 3rd with 4th and add flat 7th | Suspended tension |
| Right | Add11 | Add 11th without replacing notes | Open, modern |
| Up-left | Half-dim7 | Lower 3rd, lower 5th, add flat 7th | Bittersweet |
| Up-right | Dom9 | Add flat 7th and 9th | Smooth funk |
| Down-left | Add9 | Add 9th without 7th | Warm sparkle |
| Down-right | Min11 | Minor chord + flat 7th + 11th | Modal, open |

### 4.4 Chromatic joystick mode

With a chord held: advanced voicings and altered dominants. Left/right shift the
whole key down/up one semitone (C -> C# -> D).

| Direction | Result | Chord change | Vibe |
|---|---|---|---|
| Up | Min(Maj7) | Minor chord with natural 7th | Film noir |
| Down | Maj13 | Add 7th, 9th, and 13th | Orchestral warmth |
| Left | Key -1 semitone | Transpose | |
| Right | Key +1 semitone | Transpose | |
| Up-left | Dom7b9 | Flat 7th + flat 9th | Spanish, dark |
| Up-right | Dom7alt | Flat 7th + altered 5th and 9th | Maximum spice |
| Down-left | 6/9 | Add 6th and 9th | Jazz standard ending |
| Down-right | Half-dim7 (ø7) | Lower 3rd, lower 5th, add flat 7th | Dark jazz |

Maj7#11 and Dom13 are preset/app-only chord types, not on the joystick.

### 4.4a Borrow joystick mode (Rev 2.9)

Out-of-key chords, one gesture each. Unlike the other modes, Borrow can shift the
chord's root as well as its quality.

| Direction | Result | Rule | Example in C |
|---|---|---|---|
| Up | Secondary dominant | Same root turns dom7 | Button 2 -> D7 (V7 of G) |
| Down | Parallel maj ↔ min | Same root, flipped quality | Button 4 -> Fm |
| Left | Flat-side major | Root drops a semitone and turns major | Button 7 -> Bb; button 2 -> Db (Neapolitan) |
| Right | Passing dim7 | Root rises a semitone and turns dim7 | Button 4 -> F#°7 |
| Up-left | Tritone sub | Root moves a tritone, quality dom7 | Button 5 -> Db7 |
| Up-right | Backdoor dom7 | Root drops a whole step, quality dom7 | Button 1 -> Bb7 |
| Down-left | Borrowed 7ths | Major-family buttons take min7, minor-family buttons maj7 | Button 4 -> Fm7 |
| Down-right | Half-dim7 (ø7) | Same root turns half-diminished | Button 2 -> Dø7 |

### 4.4b OG joystick mode (Rev 2.9)

Chord selection is identical to Default and the on-screen names stay the
corrected ones. Only the notes revert to Rev 2.8: 9th chords stack a doubled 3rd
on top instead of a true 9th.

### 4.5 Chord type inventory

Support these chord types/modifications at minimum:

1. Major
2. Minor
3. Diminished
4. Flat 5 / b5
5. Augmented
6. Sus4
7. Sus2
8. Maj7
9. Min7
10. Dom7
11. Maj6
12. Min6
13. Maj9
14. Min9
15. Dom7#9
16. Half-dim7
17. Dom9
18. Add9
19. Add11
20. Min11
21. Sus4+7
22. Min(Maj7)
23. Maj13
24. 6/9
25. Maj7#11 (preset/app only)
26. Dom13 (preset/app only)
27. Dom7b9
28. Dom7alt

Combined with keys, inversions, octave shifts, and slash chords, the implementation should support a large combinatorial voicing space.

## 5. Sound Engines and Instruments

### 5.1 Synthesis architecture

Implement four synthesis engines:

- `Analog`: classic waveforms `Sine`, `Saw`, `Square`, `Triangle`.
- `FM`: modulation depth and ratio editable via app; suitable for electric pianos, bells, metallic tones.
- `Sample`: real instrument samples with pitch-shifting up to ±24 semitones; stored permanently.
- `Noise`: white, pink, filtered resonant, and metallic noise; used in hybrid presets.

### 5.2 Voice architecture

Implement 12 oscillators arranged as six stereo pairs.

| Voice | Main oscillator | Musical role | Stereo pair |
|---:|---|---|---|
| 1 | osc 0 | Root note | osc 6 |
| 2 | osc 1 | Third | osc 7 |
| 3 | osc 2 | Fifth | osc 8 |
| 4 | osc 3 | Bass: root minus 2 octaves or slash bass | osc 9 |
| 5 | osc 4 | Extension: 6th, 7th, 9th, etc. | osc 10 |
| 6 | osc 5 | Additional harmonics / fill | osc 11 |

Stereo behavior:

- Main oscillators pan across the stereo field.
- Paired oscillators pan opposite with slight detuning for width.
- When Stereo effect is off, sum to mono.

Voice count setting:

- Cycles `8 -> 4 -> 2 -> 1 oscillators`.
- `8`: thick/full with doubled notes.
- `4`: lighter chord.
- `2`: root + one other note.
- `1`: monophonic.

### 5.3 Signal flow

Process sound through this chain:

```text
Chord Logic -> 12 Voices -> ADSR -> Bass Osc -> Filter -> Chorus -> Flanger -> Delay -> Reverb -> Looper -> Output
```

This ordering should be used for audio processing or simulated state order.

### 5.4 Sound selection

Quick select:

| Chord button | Sound | Engine |
|---:|---|---|
| 1 | Saw | Analog |
| 2 | Sine | Analog |
| 3 | FM E.Piano | FM |
| 4 | FM HX7 | FM |
| 5 | Strings | Sample |
| 6 | Clarinet | Sample |
| 7 | FM Bell | FM |

Browse all sounds:

- Press Yellow/Sound button, then joystick left/right scrolls through sounds.
- Display shows sound name and animated icon.
- Manual says there are 30+ instruments and new sounds may be added by firmware updates.

### 5.5 Built-in sound list from manual

Implement these named sounds at minimum:

| Category | Sound | Description |
|---|---|---|
| Analog | Saw | Bright, buzzy, harmonically rich |
| Analog | Sine | Pure, clean fundamental tone |
| Analog | Triangle | Soft, mellow, flute-like |
| Analog | Square | Hollow, retro, 8-bit character |
| FM | FM E.Piano | DX7/Rhodes-style keys |
| FM | FM Bell | Crystalline/glass bell tone |
| FM | FM HX7 | FM synth tone available via quick select |
| Sample | Piano | Upright piano |
| Sample | Strings | Orchestral string ensemble |
| Hybrid | Juno Poly | Juno-106-style polysynth pad |
| Sample | Flute | Airy woodwind |
| Sample | Brass | Horn section |
| Hybrid | Ocean Pad | Ambient noise plus FM wash |
| Sample | Clarinet | Warm woodwind |
| Sample | Wurli | Wurlitzer electric piano |
| Sample | Vox Ahh | Choir vocal pad |
| Sample | Vibraphone | Bell-like mallet percussion |
| Sample | Acoustic | Acoustic guitar |
| Sample | Harp | Delicate plucked strings |
| Sample | Humming | Soft human humming |
| Sample | Robbo | Punchy synth bass |
| Sample | Shutter | Percussive/clicky hit |
| Hybrid | SawSquare | Layered saw and square wave |
| Special | Mic Sample | Record from mic; Batch 4+ |
| Special | User Sample | Uploaded via Companion App |

## 6. Effects and Sound Settings

### 6.1 Effects menu behavior

Controls:

- Press Yellow/Sound button to open Sounds menu.
- Joystick up enters effects list.
- Joystick left/right browses effects and settings.
- Joystick up toggles or cycles current effect/setting.
- Volume wheel adjusts effect intensity where applicable.
- If a combination exceeds CPU, show `SYSTEM FULL`; user must turn off another effect first.

### 6.2 Effects inventory

| Effect / setting | Options | Behavior / parameters |
|---|---|---|
| Reverb | `OFF`, `ON` | Adds room/hall/cathedral-like space. No extra on-device controls. |
| Delay | `OFF`, `1/4`, `1/8`, `1/16`, `1/16T` | BPM-synced echo intervals. |
| Chorus | `OFF`, `ON` | Adds widened, shimmering detuned copy. |
| Flanger | `OFF`, `ON` | Jet-plane sweeping effect. |
| Tremolo | `OFF`, `1/4`, `1/8`, `1/16`, `1/16T`, `1/32` | BPM-synced volume pulsing/chopping. |
| LFO / vibrato | `OFF`, `LOW`, `MED`, `HIGH` | Pitch wobble; LOW subtle, HIGH dramatic. |
| Filter | `OFF`, `ON` | Low-pass filter. Volume wheel sweeps cutoff. Shortcut: Red/Mode + wheel works outside menu. |
| Glide / portamento | `OFF`, `ON`; while on, speed fast to slow | Smooth pitch slides between notes; best in Lead mode. |
| Stereo | `OFF`, `ON` | ON spreads chord notes left/right with slight detune; OFF centers/mono-sums. |
| Voice Count | `8`, `4`, `2`, `1` oscillators | Controls how many notes/oscillators sound. |
| ADSR | `LONG`, `SHORT`, `SWELL`, `PLUCK`, `TOUCH`, `SUSTAIN` | Envelope presets. Fine-tune Attack and Release with wheel combos. |
| Bass | `OFF`, `ROOT`, `SLASH` | Root bass or slash bass. SLASH is Batch 4+. |
| Voice Leading | `OFF`, `ON` | Auto-select smoothest inversion. |
| Vocoder | `OFF`, `MIC`, `LOOP` | MIC uses microphone; LOOP uses Track 1 audio. |
| Scale | 10 scales | Changes button chord set. |
| Joystick Mode | `DEFAULT`, `EXTENDED`, `CHROMATIC` | Changes chord modification mapping. |
| MIDI | `OFF`, `ON` | Sends MIDI over USB-C; channel 1, up to 12 simultaneous notes. |
| USB Mode | `AUDIO`, `MIDI` | USB audio or MIDI output. Switching briefly reconnects. |
| Speaker | `OFF`, `ON` | Built-in speaker toggle. Always starts ON after power-up; not saved across reboot. |

### 6.3 ADSR presets

| Preset | Attack | Decay | Sustain | Release | Best for |
|---|---:|---:|---:|---:|---|
| LONG | 800 ms | 1000 ms | 70% | 2000 ms | Pads, ambient |
| SHORT | 100 ms | 100 ms | 100% | 120 ms | Rhythmic stabs |
| SWELL | 800 ms | 300 ms | 80% | 2000 ms | Slow builds |
| PLUCK | 5 ms | 80 ms | 0% | 180 ms | Plucked strings, keys |
| TOUCH | 25 ms | 260 ms | 66% | 450 ms | Responsive playing |
| SUSTAIN | 200 ms | 300 ms | 85% | 3000 ms | Organ-like sustain |

Manual fine-tune ranges:

- Attack: `1..2000 ms`.
- Release: `1..5000 ms`.

### 6.4 Volume wheel combos

Implement function-button + wheel real-time parameter control:

| Hold | Wheel controls | Range |
|---|---|---|
| Key/Gray | Attack time | 1–2000 ms |
| Sound/Yellow | Release time | 1–5000 ms |
| Mode/Red | Filter cutoff | 20 Hz – 20 kHz |

## 7. Randomization

Joystick click while in a menu randomizes context-specific data:

| Menu | Randomization behavior |
|---|---|
| Sounds | Random waveform / sound; effects remain unchanged |
| Key | Random everything: sound, effects, key, arp pattern |
| Mode | Random arp and sequencer pattern; sound remains unchanged |

Acceptance criteria:

- Randomization must respect available hardware batch and CPU/effect constraints.
- Randomized values must immediately update display and playback state.

## 8. Vocoder and Sampling

### 8.1 Vocoder

Batch requirements:

- `MIC` source requires Batch 4+.
- `LOOP` source uses Track 1 audio and can be modeled even if no physical mic is available.

Effect state:

- `OFF`: disabled.
- `MIC`: microphone input modulates the synth carrier.
- `LOOP`: Track 1 audio modulates the synth carrier; Track 1 audio is muted and used only as source.

Behavior:

- The vocoder analyzes the input spectrum and applies it to the currently played synth sound.
- Harmonic-rich carriers such as Saw, Square, and FM should produce stronger results than Sine.

Adjustable vocoder parameters, exposed via Companion App or MIDI CC:

| Parameter | Range | Default | Meaning |
|---|---:|---:|---|
| Formant Shift | -12..+12 semitones | 0 | Shifts vocal character up/down. |
| Band Q | 0.5..2.0 | 1.0 | Lower = wider/smoother; higher = narrower/more robotic. |
| Attack | 0.5..50 ms | 2 ms | Response speed to voice input. |
| Release | 1..200 ms | 20 ms | Sustain after input stops. |
| Noise Level | 0..100% | 15% | Adds consonant/noise articulation. |
| Gate | on/off | on | Silences output when no voice input is detected. |

### 8.2 Mic sampling

Batch 4+ only.

Behavior:

- Select `Mic Sample` sound/mode.
- Volume wheel controls mic gain before recording, range `0.5x..10x`.
- Hold chord button 1 to record from built-in mic.
- Maximum recording length is approximately 3 seconds.
- Release button to stop recording.
- Device auto-detects pitch and transposes sample to C.
- Device switches to Lead mode after recording.
- Chord buttons play the sample chromatically.
- If pitch detection fails, raw sample is used without transposition.
- Mic samples are temporary RAM data and are lost at power off unless saved to a preset.

### 8.3 User samples

Behavior:

- User Sample is uploaded via Companion App.
- User samples are stored permanently in flash and survive power cycles.
- Auto-pitching/transposition to C applies.
- Maximum length depends on flash capacity.

### 8.4 User drum kit

Behavior:

- Companion App can upload one-shot drum samples as a User Kit.
- User Kit appears when browsing drum kits only if samples are loaded.
- If no User Kit samples exist, skip it automatically.

## 9. Play Modes

### 9.1 Mode selection

Two selection methods:

1. Quick select: hold Red/Mode and press chord button 1–7 for common modes.
2. Browse: press Red/Mode, joystick left/right scrolls through all modes.

Common quick-select mapping:

| Chord button | Mode |
|---:|---|
| 1 | Play / Oneshot |
| 2 | Strum |
| 3 | Lead |
| 4 | Drone |
| 5 | Arpeggio |
| 6 | Repeat |
| 7 | Mic Sample, Batch 4+ |

The manual states there are 15 modes. Implement these named modes from the manual:

1. Play / Oneshot
2. Strum
3. Lead
4. Drone
5. Arpeggio
6. Repeat
7. Mic Sample
8. Drum Mode
9. Drum Loops
10. Auto-Drum
11. Sequencer
12. Chord Hiro
13. Ear Trainer
14. Tuner
15. Mixer

Mode-specific settings are accessed by Red/Mode menu then joystick down.

### 9.2 Play / Oneshot mode

Behavior:

- Default mode.
- Polyphonic.
- Press chord button: chord plays immediately.
- Release chord button: chord fades out according to Release setting.
- Press multiple chord buttons: all chords sound simultaneously.

### 9.3 Strum mode

Behavior:

- Press chord button: chord notes roll one by one like guitar strumming.
- Notes roll through active oscillators in order.

Strum speed setting via joystick down:

| Speed | Delay between notes | Feel |
|---|---:|---|
| Slow | 120 ms | Gentle / ballad |
| Medium | 80 ms | Natural guitar feel; default |
| Fast | 40 ms | Quick / energetic |

### 9.4 Lead mode

Behavior:

- Monophonic.
- Plays only root note of each chord.
- Pressing a new chord button cuts off previous note and starts new note.
- Best paired with Glide/Portamento and octave shifts.

### 9.5 Drone mode

Behavior:

- Press chord button once to sustain chord indefinitely.
- Press a different chord button to switch drone chord with crossfade.
- Switching to another mode stops drone.
- When switching away, drone may auto-bounce into looper if active per looper auto-bounce behavior.

### 9.6 Arpeggio mode

Behavior:

- Chord notes play one at a time automatically, synced to BPM.
- Supports six patterns, nine rates, and three chord layering modes.

Pattern selection:

- Hold Gray/Key + chord button 1–6 while in Arp mode.

| Button | Pattern | Behavior |
|---:|---|---|
| 1 | UP | Low to high, repeat |
| 2 | DOWN | High to low, repeat |
| 3 | UP/DOWN | Bounce upward then back down |
| 4 | DOWN/UP | Bounce downward then back up |
| 5 | RANDOM | Random chord tones each beat |
| 6 | FINGERPICK | Pairs of notes, guitar-like fingerpicking |

Chord layering mode:

- Hold Gray/Key + chord button 7 to cycle.

| Mode | Behavior |
|---|---|
| ARP ONLY | Only arpeggiated notes; default |
| CHORD+ARP | Full chord sustains in left channel, arp in right |
| RHYTHM+ARP | Chord pulses rhythmically while arp plays on top |

Rate setting via joystick down:

| Rate | Speed | Feel |
|---|---|---|
| 1/1 | 1 note every 4 beats | One note per bar |
| 1/2 | 1 note every 2 beats | Half notes |
| 1/4 | 1 note per beat | Quarter notes |
| 1/8 | 2 notes per beat | Eighth notes; default |
| 1/16 | 4 notes per beat | Sixteenth notes |
| 1/16T | 6 notes per beat | Triplets |
| 1/32 | 8 notes per beat | Very fast |
| Swing 8th | 2 uneven notes per beat | Shuffle feel |
| Swing 16th | 4 uneven notes per beat | Funky syncopation |

Gate length follows Release time: short release = staccato; long release = legato/overlap.

### 9.7 Repeat mode

Behavior:

- Full chord pulses on/off rhythmically, synced to BPM.
- Hold chord button to stutter/gate at selected rate.
- Rate changes with joystick down and uses same rates as Arpeggio.
- Release time is temporarily capped at 200 ms in Repeat mode for clean pulses.
- Original saved release time is restored when leaving Repeat mode.

### 9.8 Mic Sample mode

Batch 4+ only.

Behavior duplicates the sampling feature:

- Hold chord button 1 to record up to approximately 3 seconds.
- Pitch detect and transpose to C.
- Auto-switch to Lead mode.
- Chord buttons play sample chromatically.
- Volume wheel adjusts mic gain `0.5x..10x` before recording.

### 9.9 Drum Mode

Behavior:

- Each chord button triggers a drum sound.

Button mapping:

| Button | Drum sound |
|---:|---|
| 1 | Kick |
| 2 | Kick, alternate |
| 3 | Snare |
| 4 | Closed hi-hat |
| 5 | Tom |
| 6 | Bell / Ride |
| 7 | Open hi-hat / Cymbal |

### 9.10 Drum Loops mode

Behavior:

- Pre-programmed drum patterns.
- Seven styles × eight variations = 56 beats.
- Each chord button selects a style.
- Drum loops persist across mode changes so the user can switch to chord modes and jam.
- Recording a running drum loop into a looper track stops the live generator
  after capture completes, preventing a doubled beat.

Style mapping:

| Button | Style |
|---:|---|
| 1 | Rock |
| 2 | Disco |
| 3 | Reggae |
| 4 | Funk |
| 5 | Hip-Hop |
| 6 | Electro |
| 7 | Jazz |

Variations:

1. Original
2. Ghost notes
3. Busy hi-hats
4. Syncopated kicks
5. Fills
6. Half-time
7. Double-time
8. Jazz

Controls:

- Hold style button + joystick up/down cycles variations.
- Joystick left/right switches kits.

### 9.11 Auto-Drum mode

Behavior:

- Enter from Drum Mode.
- Hold a drum button and push joystick to auto-trigger that hit repeatedly at rhythmic rates.

Direction mapping:

| Direction | Rate |
|---|---|
| Up | 1/4 note |
| Right | 1/8 note |
| Down | 1/16 note |
| Left | 1/32 note |
| Up-right | Swing 8th |
| Down-right | Swing 16th |
| Down-left | 1/16 triplet |
| Up-left | Swing 8th |

### 9.12 Sequencer mode

Behavior:

- Step sequencer for chord progressions.
- Up to 16 steps.
- One chord per step.
- Loops automatically.
- Leaving Sequencer while playing auto-bounces sequence to next empty looper track.
- Looper is disabled while in Sequencer mode because the sequencer itself is the loop source.

Controls:

| Action | Control |
|---|---|
| Add chord step | Press chord button |
| Add joystick-modified chord | Hold chord button about 300 ms while choosing joystick modification |
| Start/stop playback | Joystick click |
| Navigate steps | Hold joystick left/right |
| Replace current step | Navigate to step, press new chord button |
| Add 4 steps | Joystick down, max 16 |
| Remove 4 steps | Joystick up |
| Trim to cursor | Hold joystick click + joystick up |

Step data must save:

- Chord root / button degree.
- Chord type, including joystick modifications.
- Bass mode.

### 9.13 Chord Hiro mode

Behavior:

- Rhythm-game mode inspired by Guitar Hero.
- Notes scroll across display.
- Player presses correct chord button in time.
- 16 songs/levels across 5 difficulty categories.

Categories:

| Category | Levels | Complexity |
|---|---|---|
| Folk | 1–4 | Simple progressions, slower tempo |
| Rock | 5–6 | Moderate tempo, familiar patterns |
| Jazz | 7–10 | ii-V-I patterns, chord extensions |
| Modern | 11–12 | Complex rhythms |
| Fusion | 13–16 | Advanced patterns, fast changes |

Controls:

| Action | Control |
|---|---|
| Select song | Press mode/menu action to enter song list |
| Browse levels | Joystick up/down |
| Start playing | Press mode/start action |

Timing/scoring:

| Difficulty | Timing window |
|---|---:|
| Easy | ±200 ms |
| Medium | ±150 ms |
| Hard | ±100 ms |
| Expert | ±50 ms |

Score judgments: `PERFECT`, `GREAT`, `OK`, `MISS`.

### 9.14 Ear Trainer mode

Behavior:

- HiChord plays a chord or progression.
- User identifies it by pressing correct chord button.
- Tracks streak and best streak.
- In levels involving modifications, display arrows showing joystick direction to use.

Levels:

| Level | Challenge | Rounds |
|---:|---|---:|
| 1 | Identify single chord, basic triads only | 3 |
| 2 | Identify 4-chord progression, triads only | 2 |
| 3 | Single chord with modifications such as 7ths, sus, aug; requires joystick | 3 |
| 4 | 4-chord progression with modifications | 2 |

Hint:

- Replay root note with designated hint control.

### 9.15 Tuner mode

Batch 4+ only.

Behavior:

- Chromatic tuner using built-in microphone.
- Display shows detected note and tuning offset.
- Screen inverts colors when input is in tune.

### 9.16 Mixer mode

Behavior:

- Controls looper tracks.
- Mute/unmute, volume, solo, pause/resume, metronome, multi-mute.

Controls:

| Action | Control |
|---|---|
| Mute/unmute track | Press chord button 1–6 |
| Track volume | Hold chord button + volume wheel |
| Metronome on/off | Press chord button 7 |
| Solo track | Hold chord button + press |
| Pause/resume all tracks | Joystick click |
| Multi-mute | Press 2+ chord buttons at once; Batch 4+ |

## 10. Looper

### 10.1 Capacity and model

- Six tracks.
- Approximately 20 seconds per track.
- Each track is a discrete layer.
- No overdub.
- No undo.
- Metronome on by default.
- Track 1 sets loop length; all other tracks sync to Track 1.

Track states:

- `OFF` / empty.
- `WAITING`.
- `RECORD` / recording.
- `LOOP` / playing.
- `PAUSED` may be modeled globally or per-track.

### 10.2 Recording Track 1

Flow:

1. Joystick click opens looper; display shows `WAITING`.
2. While waiting, joystick left/right selects bar count.
3. Bar count `0`: free length, user stops manually.
4. Bar count `1..8`: fixed length; auto-stops after selected bars.
5. In bar mode, a 4-beat metronome count-in plays before recording.
6. User plays.
7. Joystick click stops recording.
8. Loop starts playback immediately.

### 10.3 Adding tracks 2–6

Behavior:

- User can change sound, mode, key/octave, etc. for a new layer.
- Click sequence enters waiting/recording.
- New track begins recording at Track 1's next loop point for sync.
- On stop, track loops in sync.
- After finishing a recording, auto-advance to next empty track.
- Manual track navigation via joystick left/right, or up/down in drum modes.

### 10.4 Looper display

OLED shows two tracks at a time across three pages:

| Page shape | Tracks |
|---|---|
| Circle | 1–2 |
| Square | 3–4 |
| Triangle | 5–6 |

State symbols:

- Filled / `PLAY`: track playing.
- Outline / `—`: empty.
- `R` / `REC`: recording now.

### 10.5 Looper controls

| Action | Control |
|---|---|
| Switch tracks | Joystick left/right; in drum modes up/down |
| Cycle looper state | Joystick click: `OFF -> WAITING -> RECORD -> LOOP -> OFF` |
| Set bar count | Joystick left/right while `WAITING`; `0` free, `1..8` fixed |
| Pause/resume all | Joystick down |
| Clear current track | Joystick click when selected track is `OFF` / stopped |
| Clear all tracks | Stop Track 1 |
| Toggle metronome | Button 7 in Mixer mode |

### 10.6 Generator capture

Behavior:

- A running Drum Loop continues across mode changes.
- The user records it through the normal looper controls.
- After a successful fixed or FREE capture reaches playback, the live drum
  generator stops and the captured mono track continues.

Acceptance criteria:

- Captured tracks obey the 6-track limit and normal looper state rules.
- Cancelling or clearing a recording does not stop the live generator.
- Manual drum-pad recordings do not trigger generator handoff.

## 11. Drums and Beats

### 11.1 Drum kits

Hold designated modifier + chord button to switch drum kits.

| Button | Kit | Style |
|---:|---|---|
| 1 | Tight Kit | Clean default PCM samples |
| 2 | x0x Box | 808-style deep sub bass |
| 3 | x9x Box | 909-style punchy techno |
| 4 | Lynn Kit | LinnDrum-style warm vintage |
| 5 | KR-78 | Lo-fi retro machine |
| 6 | Trap Box | Modern sub-heavy trap |
| 7 | Tight Kit | Cycles back to default |

Additional kit:

- `User Kit` appears if uploaded through Companion App.
- Skipped if no user samples are loaded.

### 11.2 Drum sound mapping

Same as Drum Mode:

| Button | Sound |
|---:|---|
| 1 | Kick |
| 2 | Alternate kick |
| 3 | Snare |
| 4 | Closed hi-hat |
| 5 | Tom |
| 6 | Bell / Ride |
| 7 | Open hi-hat / Cymbal |

### 11.3 Drum loops

Implement 7 styles × 8 variations = 56 loops:

Styles: Rock, Disco, Reggae, Funk, Hip-Hop, Electro, Jazz.

Variations: Original, Ghost notes, Busy hi-hats, Syncopated kicks, Fills, Half-time, Double-time, Jazz.

Controls:

- Chord button selects style.
- Hold style button + joystick up/down cycles variation.
- Joystick left/right switches kit.

### 11.4 Auto-drum

Auto-trigger drum hits by rhythmic rate while holding a drum button and joystick direction. Use the rate table in section 9.11.

## 12. Connectivity and MIDI

### 12.1 Audio outputs

- Built-in mono speaker.
- Speaker auto-mutes when headphones are connected.
- Speaker can be toggled off in effects/settings.
- Speaker always starts ON after power cycle.
- 3.5 mm stereo headphone/line out.
- USB Audio: 48 kHz stereo, class-compliant, no drivers.

### 12.2 USB mode

Only one USB mode is active at a time:

- `AUDIO`: USB sends stereo audio to DAW/computer/device.
- `MIDI`: USB sends MIDI notes, no audio over USB.

Switching USB mode causes brief disconnect/reconnect.

### 12.3 MIDI behavior

- USB MIDI is class-compliant.
- MIDI channel: 1.
- Up to 12 simultaneous notes.
- Fixed velocity: 100.
- MIDI toggle must be ON.
- TRS MIDI Type A output is available on Batch 4+.
- TRS MIDI is always active when MIDI is enabled.

Acceptance criteria:

- Chord buttons emit appropriate MIDI note-on/note-off events for all currently generated chord tones.
- Play modes transform MIDI output similarly to audio output: arps produce timed note sequences, Repeat gates notes, Lead is monophonic, Drum Mode sends drum notes or internal drum triggers.
- USB Audio and USB MIDI cannot be simultaneously active.

## 13. Companion App Features

The Companion App is browser-based and communicates over Web MIDI. It should be represented as either a separate UI or extended settings editor.

Features:

### 13.1 Preset Designer

- Visual editing of all parameters.
- Effects, ADSR, filter, chorus depth, flanger rate, and more.
- Save directly to 4 hardware preset slots.

### 13.2 Sample Upload

- Upload audio as `User Sample` playable instrument.
- Upload one-shot drum hits as `User Kit`.
- Stored permanently in flash.
- Survives power cycles.

### 13.3 Arp Pattern Editor

- Design custom fingerpick patterns.
- Supports note pairs and octave multipliers.
- Allows patterns not available from device alone.

### 13.4 Vocoder Controls

Expose and edit:

- Formant shift.
- Band Q.
- Attack.
- Release.
- Noise level.
- Gate threshold / gate behavior.

### 13.5 Hybrid Voice Editor

Expose and edit:

- Individual oscillator engines.
- FM ratios.
- Detune amounts.
- Panning.
- Gain.
- Hybrid sound layering.

## 14. User Presets

### 14.1 Preset slots

- Four user preset slots: `P1..P4`.
- Presets survive power cycles and factory resets.
- Saved slots are marked with `*` on display.

Controls:

- Open preset menu with designated button combo.
- Joystick left/right selects slot.
- Joystick up saves.
- Joystick down loads.

### 14.2 Data saved in preset

Presets should save at least:

- Selected sound.
- All effect states and parameters.
- Key.
- Scale.
- Mode.
- BPM.
- Arp pattern.
- Arp rate.
- Arp chord layering mode.
- Global octave shift.
- Per-button octave shifts.
- Inversions.
- Chord locks.
- Filter cutoff.
- Drum kit.
- Drum loop style and variation.
- Sequencer pattern.
- Vocoder settings.
- FM parameters.
- User/mic samples where applicable.
- Hybrid voice settings.

Manual describes this as 100+ parameters.

## 15. Battery and Power

### 15.1 Power states

- `off`: no interaction.
- `on`: normal operation.
- `sleep`: after inactivity; wake with any button.
- `deepSleep`: low battery protective state.
- `dfu`: firmware update mode.

### 15.2 Charging

- USB-C charging.
- Full charge around 2 hours.
- Device can play while charging.

### 15.3 Battery life by batch

| Batch | Battery life |
|---|---|
| Batch 1 | about 3–4 hours, varies by unit |
| Batch 2 | about 4–5 hours |
| Batch 4+ | about 8–10 hours |

### 15.4 Battery meter

- Batch 2+ can show battery percentage.
- Batch 1 has no battery meter.
- Battery status opened with button combo.

### 15.5 Auto-sleep and low battery

- Auto-sleep after 30 seconds of inactivity.
- Wake instantly by pressing any button.
- Settings are preserved across sleep.
- At approximately 3% battery, enter deep sleep to protect Li-Po cell.

## 16. Advanced Controls

### 16.1 Button combos

Implement combo actions as configurable input bindings:

| Combo behavior | Action |
|---|---|
| Battery combo | Show battery voltage and percentage |
| Factory reset combo | Factory reset; presets and looper recordings remain safe |
| Hold firmware combo 5 seconds | Enter DFU / firmware update mode |
| Preset combo | Open preset menu |
| Chord + joystick + lock combo | Lock/unlock chord modification |
| Hold chord + inversion shortcut | Cycle inversion root/1st/2nd |
| Hold chord + octave down shortcut | Per-button octave down |
| Hold chord + octave up shortcut | Per-button octave up |

Because the web manual renders icon buttons without text labels in several rows, keep physical combo identifiers abstract/configurable rather than hard-coding unknown icon names.

### 16.2 Factory reset

Behavior:

- Restore factory/default global settings.
- Do not erase user presets.
- Do not erase looper recordings.

### 16.3 DFU / firmware mode

Behavior:

- Holding firmware combo for 5 seconds enters DFU mode.
- Used by external updater.
- Batch 1 should be fully charged before update.

## 17. Calibration

Batch 1–3 only.

### 17.1 Calibration entry

- Power off.
- Hold Key + Sound while powering on.
- Display shows `ENTERING CALIBRATE`.

### 17.2 Button calibration

For chord buttons 1–7:

- Display prompts `HOLD N — PRESS+HOLD`.
- User presses and holds button.
- Device takes 20 samples and rejects outliers.
- Display shows `BTN N OK` with a value.
- If timeout occurs, retry automatically.

### 17.3 Joystick calibration

- Display shows `DONT TOUCH`; capture center for 2 seconds.
- Prompt `PUSH LEFT`; user pushes/holds until confirmed.
- Repeat for right, up, down.

### 17.4 Completion

- Validate calibration data.
- On success: display `DONE! SAVED`, restart device.
- On failure: display `ERROR BAD CAL`; user power cycles and retries.
- Calibration is permanent and survives power cycles and firmware updates.
- Batch 4+ uses digital I2C buttons and does not need calibration.

## 18. Technical Specs to Model

| Spec | Value |
|---|---|
| Processor | STM32H750, 400 MHz Cortex-M7 |
| Sample rate | 48 kHz |
| Bit depth | 16-bit output / 32-bit float internal |
| Polyphony | 12 oscillators / 6 stereo pairs |
| Display | 64×32 OLED |
| Latency | <3 ms target |
| Dynamic range | ~90 dB target |
| Storage | QSPI Flash, non-volatile |
| Looper storage | SDRAM, about 20 sec × 6 tracks |
| MIDI | USB class-compliant channel 1; TRS Type A on Batch 4+ |
| Power | USB-C / rechargeable Li-Po |
| Battery model | Batch 1–2: LP503450 1000 mAh 3.7 V; Batch 4+: LP703478 2400 mAh 3.7 V |
| Charger | TP4056 USB-C, about 2 hr charge |
| Audio out | 3.5 mm stereo, USB, built-in speaker |

## 19. Troubleshooting / Error States

Implement diagnostic states and messages for:

- No sound: check volume, battery, current mode, headphones, speaker toggle.
- Buttons not responding: check battery, stuck function button, power cycle, calibration for Batch 1–3.
- Looper problems: check selected track, bar count, mixer mutes, pause/resume state.
- Display frozen: check battery, sleep state, power cycle.
- USB audio silent: require `USB_MODE = AUDIO`, working cable, DAW restart.
- MIDI not sending: require `USB_MODE = MIDI`, MIDI toggled ON, DAW listening on channel 1.
- System Full: CPU cannot enable requested effect combination; user must disable another effect.
- No empty looper track: cannot bounce/record until user clears a track.
- Batch-incompatible feature: show clear unavailable message.

## 20. Suggested Acceptance Test Matrix

### 20.1 Chord generation

- In C Major, buttons produce C, Dm, Em, F, G, Am, Bdim.
- Changing key to G transposes the same degree relationships.
- Changing scale alters available notes/chords while preserving seven-button workflow.
- Global octave and per-button octave shifts combine correctly.
- Inversions reorder chord tones without changing chord identity.
- Voice Leading chooses smaller movements than fixed root-position playback.
- Chord Lock persists joystick modifications through key changes.
- Bass ROOT adds root two octaves below.
- Bass SLASH on Batch 4+ supports independent bass/chord buttons.

### 20.2 Joystick

- Default mode directions produce all listed default chord modifications.
- Extended mode directions produce listed extended voicings.
- Chromatic mode with chord held produces listed advanced voicings.
- Chromatic mode without chord held shifts key by semitone left/right.
- Releasing joystick reverts modification unless locked.

### 20.3 Effects

- Effect menu cycles/toggles all effects.
- Delay and Tremolo are BPM-synced.
- Filter cutoff responds to wheel and shortcut.
- ADSR presets apply exact parameter values.
- Attack/Release wheel combos respect ranges.
- Glide causes smooth pitch transitions in Lead mode.
- Stereo toggle switches between wide paired oscillators and mono.
- System Full prevents excessive effect combinations.

### 20.4 Performance modes

- Play mode supports simultaneous chords.
- Strum mode rolls notes at 120/80/40 ms.
- Lead mode is monophonic.
- Drone sustains until mode switch.
- Arp supports 6 patterns, 9 rates, and 3 layering modes.
- Repeat gates full chords and caps release at 200 ms while active.
- Sequencer records, edits, loops, trims, and bounces up to 16 steps.
- Chord Hiro implements timing windows and score judgments.
- Ear Trainer implements levels, rounds, hints, streaks.
- Tuner unavailable before Batch 4 and functional for Batch 4+.
- Mixer controls mutes, volumes, solo, pause, metronome, and Batch 4+ multi-mute.

### 20.5 Looper/drums

- Track 1 defines loop length.
- Bar count 0 is free; 1–8 is fixed with 4-beat count-in.
- Tracks 2–6 sync to Track 1.
- Auto-advance after recording.
- Display pages show two tracks each with page shapes.
- Drum Mode button mapping is correct.
- Seven kits are selectable, plus conditional User Kit.
- Drum Loops implement 56 style/variation combinations.
- Auto-Drum direction-rate mapping is correct.
- Capturing a Drum Loop stops its live generator only after successful recording.

### 20.6 I/O, presets, power

- USB Audio and USB MIDI are mutually exclusive.
- MIDI note output is channel 1, velocity 100, up to 12 notes.
- Speaker auto-mutes on headphone insert and resets ON after power-up.
- Presets P1–P4 save and load 100+ parameter-equivalent state.
- Battery display unavailable on Batch 1.
- Auto-sleep after 30 seconds preserves state.
- Deep sleep at about 3% battery.
- Factory reset preserves presets and looper recordings.
- Calibration only available for Batch 1–3.

## 21. Non-Goals / Unknowns from Manual

- Exact DSP algorithms, sample assets, factory preset data, and waveform tables are not included in the public manual.
- Several button-combo icons were not text-rendered in the manual; expose those as configurable input bindings.
- The manual says 30+ instruments, but the visible full sound list contains the named sounds above. Design sound registry to allow additional firmware sounds.
- CPU budget/effect compatibility rules are not exhaustively specified; implement a generic budget system and at least block known excessive combinations.
