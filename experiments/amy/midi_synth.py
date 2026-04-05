import argparse
import time

import amy

# Control constants
NUM_OSC = 64
BASE_MIDI = 0

WAVE_NAME_TO_CONST = {
    'sine': amy.SINE,
    'saw_down': amy.SAW_DOWN,
    'saw_up': amy.SAW_UP,
    'triangle': amy.TRIANGLE,
    'pulse': amy.PULSE,
    'noise': amy.NOISE,
}

EG0_NAME_TO_CONST = {
    'normal': amy.ENVELOPE_NORMAL,
    'linear': amy.ENVELOPE_LINEAR,
    'dx7': amy.ENVELOPE_DX7,
    'true_exponential': amy.ENVELOPE_TRUE_EXPONENTIAL,
}

active_voices = {}
osc_to_note = {}
next_osc = 0
filter_cutoff = 1200.0
filter_resonance = 0.8

VOICE_PRESETS = {
    'fat': {
        'wave': 'saw_down',
        'envelope': 'normal',
        'bp0': '0,0,25,1,220,0.82,900,0',
        'intervals': [0],
        'detune': [-0.09, 0.0, 0.09],
        'amp_scale': 0.34,
        'pan_spread': 0.35,
        'fx': {'chorus_level': 0.3, 'chorus_freq': 0.45, 'chorus_amp': 0.55, 'reverb_level': 0.18, 'echo_level': 0.0},
    },
    'dream': {
        'wave': 'triangle',
        'envelope': 'true_exponential',
        'bp0': '0,0,40,1,500,0.75,1400,0',
        'intervals': [0, 7, 12],
        'detune': [-0.04, 0.04],
        'amp_scale': 0.28,
        'pan_spread': 0.55,
        'fx': {'chorus_level': 0.45, 'chorus_freq': 0.28, 'chorus_amp': 0.75, 'reverb_level': 0.35, 'echo_level': 0.08, 'echo_delay_ms': 360, 'echo_feedback': 0.22},
    },
    'organ': {
        'wave': 'pulse',
        'envelope': 'linear',
        'bp0': '0,1,900,0.92,80,0',
        'intervals': [0, 12],
        'detune': [0.0],
        'amp_scale': 0.45,
        'pan_spread': 0.15,
        'fx': {'chorus_level': 0.12, 'chorus_freq': 0.2, 'chorus_amp': 0.22, 'reverb_level': 0.1, 'echo_level': 0.0},
    },
    'blade': {
        'wave': 'saw_down',
        'envelope': 'true_exponential',
        'bp0': '0,0,35,1,900,0.78,2600,0.52,3600,0',
        'intervals': [0, 7, 12],
        'detune': [-0.07, 0.07],
        'amp_scale': 0.6,
        'pan_spread': 0.72,
        'fx': {
            'chorus_level': 0.48,
            'chorus_freq': 0.19,
            'chorus_amp': 0.78,
            'reverb_level': 0.28,
            'reverb_liveness': 0.9,
            'reverb_damping': 0.36,
            'reverb_xover_hz': 2200,
            'echo_level': 0.08,
            'echo_delay_ms': 420,
            'echo_feedback': 0.24,
            'echo_filter_coef': 0.18,
        },
    },
}


def midi_to_freq(midi_note: int) -> float:
    return 440.0 * (2.0 ** ((midi_note - 69) / 12.0))


def detune_ratio(detune_semitones: float) -> float:
    return 2.0 ** (detune_semitones / 12.0)


def alloc_osc(note_id: int) -> int:
    global next_osc

    osc = next_osc
    next_osc = (next_osc + 1) % NUM_OSC

    stolen_note = osc_to_note.pop(osc, None)
    if stolen_note is not None:
        stolen_bundle = active_voices.get(stolen_note)
        if stolen_bundle is not None:
            stolen_bundle['oscs'] = [candidate for candidate in stolen_bundle['oscs'] if candidate != osc]
            if not stolen_bundle['oscs']:
                active_voices.pop(stolen_note, None)
        amy.send(osc=osc, vel=0)

    osc_to_note[osc] = note_id
    return osc


def apply_filter_to_all(filter_freq: float, resonance: float) -> None:
    for osc in range(NUM_OSC):
        amy.send(
            osc=osc,
            filter_type=amy.FILTER_LPF,
            filter_freq=filter_freq,
            resonance=resonance,
        )


def apply_fx(preset_fx: dict) -> None:
    amy.chorus(
        level=preset_fx.get('chorus_level', 0.0),
        freq=preset_fx.get('chorus_freq', 0.25),
        amp=preset_fx.get('chorus_amp', 0.5),
    )
    amy.reverb(
        level=preset_fx.get('reverb_level', 0.0),
        liveness=preset_fx.get('reverb_liveness', 0.85),
        damping=preset_fx.get('reverb_damping', 0.5),
        xover_hz=preset_fx.get('reverb_xover_hz', 3000.0),
    )
    amy.echo(
        level=preset_fx.get('echo_level', 0.0),
        delay_ms=preset_fx.get('echo_delay_ms', 420),
        feedback=preset_fx.get('echo_feedback', 0.18),
        filter_coef=preset_fx.get('echo_filter_coef', 0.08),
    )


def all_notes_off() -> None:
    for osc in range(NUM_OSC):
        amy.send(osc=osc, vel=0)
    active_voices.clear()
    osc_to_note.clear()


def note_on(
    midi_note: int,
    velocity: int,
    wave: int,
    eg0_type: int,
    filter_freq: float,
    resonance: float,
    bp0: str,
    chord_intervals: list[int],
    detune_offsets: list[float],
    amp_scale: float,
    pan_spread: float,
) -> None:
    if velocity <= 0:
        note_off(midi_note)
        return

    note_off(midi_note)

    base_vel = max(0.05, min(1.0, velocity / 127.0))
    oscs = []
    layers = max(1, len(chord_intervals) * len(detune_offsets))
    layer_vel = max(0.03, min(1.0, base_vel * amp_scale * (3.0 / layers)))

    total_layers = len(chord_intervals) * len(detune_offsets)
    layer_index = 0
    for interval in chord_intervals:
        note_freq = midi_to_freq(midi_note + interval)
        for detune in detune_offsets:
            osc = alloc_osc(midi_note)
            if total_layers == 1:
                pan = 0.5
            else:
                pan = 0.5 + (((layer_index / (total_layers - 1)) * 2.0) - 1.0) * (pan_spread * 0.5)
                pan = max(0.0, min(1.0, pan))
            amy.send(
                osc=osc,
                wave=wave,
                freq=note_freq * detune_ratio(detune),
                vel=layer_vel,
                bp0=bp0,
                eg0_type=eg0_type,
                filter_type=amy.FILTER_LPF,
                filter_freq=filter_freq,
                resonance=resonance,
                pan=pan,
            )
            oscs.append(osc)
            layer_index += 1

    active_voices[midi_note] = {'oscs': oscs}


def note_off(midi_note: int) -> None:
    voice = active_voices.pop(midi_note, None)
    if voice is None:
        return

    for osc in voice['oscs']:
        osc_to_note.pop(osc, None)
        amy.send(osc=osc, vel=0)


def main() -> None:
    parser = argparse.ArgumentParser(description='AMY MIDI synth')
    parser.add_argument('--midi-port', '-p', default=None, help='MIDI input port name (default: first available)')
    parser.add_argument('--preset', choices=VOICE_PRESETS.keys(), default='blade', help='Voice preset')
    parser.add_argument('--wave', '-w', choices=WAVE_NAME_TO_CONST.keys(), default=None, help='Wave type override')
    parser.add_argument('--cutoff', '-f', type=float, default=1200.0, help='Lowpass filter cutoff in Hz')
    parser.add_argument('--resonance', '-r', type=float, default=0.8, help='Lowpass filter resonance')
    parser.add_argument('--envelope', '-e', choices=EG0_NAME_TO_CONST.keys(), default=None, help='Envelope type override')
    parser.add_argument('--chord', choices=['off', 'fifth', 'major', 'minor', 'sus2', 'sus4', 'seventh'], default='off', help='Extra chord tones per note')
    parser.add_argument('--detune', type=float, default=None, help='Detune spread in semitones for layered oscillators')
    args = parser.parse_args()

    try:
        import mido
    except ImportError:
        print('Error: mido is required for MIDI input. Install with pip install mido python-rtmidi')
        return

    portname = args.midi_port
    input_names = mido.get_input_names()
    if not input_names:
        print('Error: No MIDI input ports found. Connect a device and retry.')
        return

    if not portname:
        portname = input_names[0]

    preset = VOICE_PRESETS[args.preset]
    wave_name = args.wave if args.wave else preset['wave']
    envelope_name = args.envelope if args.envelope else preset['envelope']
    wave = WAVE_NAME_TO_CONST[wave_name]
    eg0_type = EG0_NAME_TO_CONST[envelope_name]
    bp0 = preset['bp0']
    detune_offsets = list(preset['detune'])

    if args.detune is not None:
        if len(detune_offsets) == 1:
            detune_offsets = [0.0]
        else:
            detune_offsets = [-abs(args.detune), 0.0, abs(args.detune)]

    chord_map = {
        'off': preset['intervals'],
        'fifth': [0, 7],
        'major': [0, 4, 7],
        'minor': [0, 3, 7],
        'sus2': [0, 2, 7],
        'sus4': [0, 5, 7],
        'seventh': [0, 4, 7, 10],
    }
    chord_intervals = chord_map[args.chord]

    global filter_cutoff, filter_resonance
    filter_cutoff = args.cutoff
    filter_resonance = args.resonance

    amy.live(default_synths=0, audio_in=0, startup_bleep=0)
    amy.reset()

    apply_fx(preset['fx'])
    apply_filter_to_all(filter_cutoff, filter_resonance)

    print('AMY MIDI Synth running')
    print(f'MIDI input port: {portname}')
    print(
        f'Preset: {args.preset}, Wave: {wave_name}, Envelope: {envelope_name}, '
        f'Chord: {args.chord}, Layers: {len(chord_intervals) * len(detune_offsets)}'
    )
    print(f'Cutoff: {filter_cutoff}, Resonance: {filter_resonance}')

    try:
        with mido.open_input(portname) as inport:
            for msg in inport:
                if msg.type == 'note_on':
                    note_on(
                        msg.note,
                        msg.velocity,
                        wave,
                        eg0_type,
                        filter_cutoff,
                        filter_resonance,
                        bp0,
                        chord_intervals,
                        detune_offsets,
                        preset['amp_scale'],
                        preset.get('pan_spread', 0.3),
                    )
                elif msg.type == 'note_off':
                    note_off(msg.note)
                elif msg.type == 'control_change' and msg.control in (74, 71):
                    # optional real-time mapping
                    if msg.control == 74:
                        filter_cutoff = max(50.0, min(12000.0, 10.0 + msg.value * 100.0))
                    elif msg.control == 71:
                        filter_resonance = max(0.5, min(16.0, 0.5 + msg.value / 8.0))
                    apply_filter_to_all(filter_cutoff, filter_resonance)
    except KeyboardInterrupt:
        pass
    finally:
        all_notes_off()


if __name__ == '__main__':
    main()
