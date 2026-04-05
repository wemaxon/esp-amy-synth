import math
import os
import sys
import time
import msvcrt

import amy

# -----------------------------
# Config
# -----------------------------
BASE_MIDI = 48   # C3
OCTAVE = 0
WAVE = amy.SAW_DOWN
OSC_START = 0
MAX_OSCS_PER_KEY = 3

# Beginner mode states
BEGINNER_MODE = False
PENTATONIC_MODE = False
CHORD_MODE = False
USE_WAVE = True
EG0_TYPE = amy.ENVELOPE_NORMAL

WAVE_OPTIONS = [amy.SINE, amy.SAW_DOWN, amy.SAW_UP, amy.TRIANGLE, amy.PULSE, amy.NOISE]
WAVE_NAMES = ["SINE", "SAW_DOWN", "SAW_UP", "TRIANGLE", "PULSE", "NOISE"]
ENVELOPE_OPTIONS = [amy.ENVELOPE_NORMAL, amy.ENVELOPE_LINEAR, amy.ENVELOPE_DX7, amy.ENVELOPE_TRUE_EXPONENTIAL]
ENVELOPE_NAMES = ["NORMAL", "LINEAR", "DX7", "TRUE_EXPONENTIAL"]

# AMY filter params:
# filter_type: 1 = lowpass
# filter_freq: center/break frequency control
# resonance: 0.5 - 16.0
filter_cutoff = 1200.0
filter_resonance = 0.8

KEY_TO_SEMITONE = {
    "y": 0,   # C
    "s": 1,   # C#
    "x": 2,   # D
    "d": 3,   # D#
    "c": 4,   # E
    "v": 5,   # F
    "g": 6,   # F#
    "b": 7,   # G
    "h": 8,   # G#
    "n": 9,   # A
    "j": 10,  # A#
    "m": 11,  # B
    ",": 12,  # C
    "l": 13,  # C#
    ".": 14,  # D
    "ö": 15,  # D#
    "-": 16,  # E
}

OSC_FOR_KEY = {
    k: OSC_START + (i * MAX_OSCS_PER_KEY)
    for i, k in enumerate(KEY_TO_SEMITONE)
}
TOTAL_OSC = OSC_START + (len(KEY_TO_SEMITONE) * MAX_OSCS_PER_KEY)
pressed = set()
active_chords = {}

PENTATONIC_SCALE = [0, 2, 4, 7, 9]
PENTATONIC_SEMITONES = [
    0, 2, 4, 7, 9,
    12, 14, 16, 19, 21,
    24, 26, 28, 31, 33,
    36, 38,
]


def midi_to_freq(midi_note: int) -> float:
    return 440.0 * (2.0 ** ((midi_note - 69) / 12.0))


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def apply_filter_to_all_oscs() -> None:
    for osc in range(OSC_START, TOTAL_OSC):
        amy.send(
            osc=osc,
            filter_type=amy.FILTER_LPF,
            filter_freq=filter_cutoff,
            resonance=filter_resonance,
        )


def note_on(key: str) -> None:
    if PENTATONIC_MODE:
        semitone = PENTATONIC_SEMITONES[list(KEY_TO_SEMITONE.keys()).index(key)]
    else:
        semitone = KEY_TO_SEMITONE[key]

    root_osc = OSC_FOR_KEY[key]
    root_midi = BASE_MIDI + (OCTAVE * 12) + semitone

    chord_intervals = [0]
    if CHORD_MODE:
        chord_intervals = [0, 4, 7]

    active = []
    for i, interval in enumerate(chord_intervals):
        midi_note = root_midi + interval
        freq = midi_to_freq(midi_note)
        osc = root_osc + i

        params = {
            "osc": osc,
            "freq": freq,
            "vel": 1,
            "bp0": "10,1,50,0.9,200,0",
            "filter_type": amy.FILTER_LPF,
            "filter_freq": filter_cutoff,
            "resonance": filter_resonance,
        }
        if USE_WAVE:
            params["wave"] = WAVE
        if EG0_TYPE is not None:
            params["eg0_type"] = EG0_TYPE

        amy.send(**params)
        active.append(osc)

    active_chords[key] = active

def note_off(key: str) -> None:
    if key in active_chords:
        for osc in active_chords.pop(key, []):
            amy.send(osc=osc, vel=0)
    else:
        osc = OSC_FOR_KEY[key]
        amy.send(osc=osc, vel=0)


def all_notes_off() -> None:
    # Clear all active keys first.
    for key in list(pressed):
        note_off(key)
    pressed.clear()

    # Ensure any staled oscillator is turned off.
    for osc in range(TOTAL_OSC):
        amy.send(osc=osc, vel=0)

    active_chords.clear()


def redraw() -> None:
    os.system("cls")
    print("AMY Console Synth")
    print()
    print("Notes: y s x d c v g b h n j m , l . ö -")
    print("Filter: Left/Right = cutoff, Up/Down = resonance")
    print("Octave: [ and ]")
    print("Space = all notes off")
    print("Q = quit")
    print("P = toggle pentatonic mode")
    print("C = toggle chord mode")
    print("B = toggle beginner mode")
    print("W = cycle wave type")
    print("E = cycle envelope type")
    print("T = toggle explicit wave on/off")
    print()

    wave_name = "OFF" if not USE_WAVE else WAVE_NAMES[WAVE_OPTIONS.index(WAVE)]
    envelope_name = ENVELOPE_NAMES[ENVELOPE_OPTIONS.index(EG0_TYPE)]

    print(f"Beginner mode:     {'ON' if BEGINNER_MODE else 'OFF'}")
    print(f"Pentatonic mode:   {'ON' if PENTATONIC_MODE else 'OFF'}")
    print(f"Chord mode:        {'ON' if CHORD_MODE else 'OFF'}")
    print(f"Wave:              {wave_name}")
    print(f"Envelope:          {envelope_name}")
    print(f"Octave shift:      {OCTAVE:+d}")
    print(f"Cutoff:            {filter_cutoff:.1f} Hz")
    print(f"Resonance:         {filter_resonance:.2f}")
    print(f"Held notes:        {' '.join(sorted(pressed)) if pressed else 'none'}")


def read_key():
    """
    Returns:
      ('char', 'z') for normal keys
      ('arrow', 'left'/'right'/'up'/'down') for arrows
      None if nothing available
    """
    if not msvcrt.kbhit():
        return None

    ch = msvcrt.getwch()

    # Windows special keys
    if ch in ("\x00", "\xe0"):
        code = msvcrt.getwch()
        arrows = {
            "K": "left",
            "M": "right",
            "H": "up",
            "P": "down",
        }
        if code in arrows:
            return ("arrow", arrows[code])
        return None

    return ("char", ch.lower())


def main() -> None:
    global OCTAVE, filter_cutoff, filter_resonance
    global BEGINNER_MODE, PENTATONIC_MODE, CHORD_MODE, WAVE, EG0_TYPE, USE_WAVE

    amy.live(default_synths=1, audio_in=0)
    # amy.chorus(level=0)
    # amy.echo(level=0, feedback=0)
    # amy.reverb(level=0)
    apply_filter_to_all_oscs()
    redraw()

    while True:
        event = read_key()
        if event is None:
            time.sleep(0.005)
            continue

        kind, value = event

        if kind == "arrow":
            if value == "left":
                filter_cutoff = clamp(filter_cutoff * 0.85, 50.0, 12000.0)
                apply_filter_to_all_oscs()
                redraw()
            elif value == "right":
                filter_cutoff = clamp(filter_cutoff * 1.15, 50.0, 12000.0)
                apply_filter_to_all_oscs()
                redraw()
            elif value == "up":
                filter_resonance = clamp(filter_resonance + 0.1, 0.5, 16.0)
                apply_filter_to_all_oscs()
                redraw()
            elif value == "down":
                filter_resonance = clamp(filter_resonance - 0.1, 0.5, 16.0)
                apply_filter_to_all_oscs()
                redraw()
            continue

        ch = value

        if ch == "q":
            break

        if ch == " ":
            all_notes_off()
            redraw()
            continue

        if ch == "[":
            OCTAVE = max(-2, OCTAVE - 1)
            redraw()
            continue

        if ch == "]":
            OCTAVE = min(3, OCTAVE + 1)
            redraw()
            continue

        if ch == "p":
            PENTATONIC_MODE = not PENTATONIC_MODE
            redraw()
            continue

        if ch == "c":
            CHORD_MODE = not CHORD_MODE
            redraw()
            continue

        if ch == "b":
            BEGINNER_MODE = not BEGINNER_MODE
            if BEGINNER_MODE:
                PENTATONIC_MODE = True
                CHORD_MODE = True
                WAVE = amy.SINE
                EG0_TYPE = amy.ENVELOPE_TRUE_EXPONENTIAL
            else:
                PENTATONIC_MODE = False
                CHORD_MODE = False
                WAVE = amy.SAW_DOWN
                EG0_TYPE = amy.ENVELOPE_NORMAL
            redraw()
            continue

        if ch == "w":
            idx = WAVE_OPTIONS.index(WAVE)
            idx = (idx + 1) % len(WAVE_OPTIONS)
            WAVE = WAVE_OPTIONS[idx]
            redraw()
            continue

        if ch == "e":
            idx = ENVELOPE_OPTIONS.index(EG0_TYPE)
            idx = (idx + 1) % len(ENVELOPE_OPTIONS)
            EG0_TYPE = ENVELOPE_OPTIONS[idx]
            redraw()
            continue

        if ch == "t":
            USE_WAVE = not USE_WAVE
            redraw()
            continue

        if ch in KEY_TO_SEMITONE:
            # Toggle note on/off with repeated keypress in console mode.
            # Console input doesn't reliably provide key-up events.
            if ch in pressed:
                note_off(ch)
                pressed.remove(ch)
            else:
                note_on(ch)
                pressed.add(ch)
            redraw()

    all_notes_off()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        all_notes_off()
        sys.exit(0)
