"""Client for the hardware-in-the-loop console (components/hil_console,
commands in main/hil/hil_app.h).

Every command gets one reply line, '@' followed by JSON; bulk data arrives
before it as '@=' base64 lines. Other lines are device logs, kept in .log.
"""

import array
import base64
import json
import os
import time
from dataclasses import dataclass

import serial

DEFAULT_PORT = os.environ.get("HIL_PORT", "/dev/ttyACM0")


class DeviceError(RuntimeError):
    pass


@dataclass
class Capture:
    source: str
    rate: int
    first_frame: int     # looper audio-frame index of samples[0]
    samples: array.array  # mono int16


class Device:
    def __init__(self, port=DEFAULT_PORT):
        self._serial = serial.Serial()
        self._serial.port = port
        self._serial.baudrate = 115200
        self._serial.timeout = 0.05
        # Opening with DTR/RTS low keeps the chip running (no reset).
        self._serial.dtr = False
        self._serial.rts = False
        self._serial.open()
        self._pending = b""
        self.log = []

    def close(self):
        self._serial.close()

    def _lines(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._pending += self._serial.read(65536)
            while b"\n" in self._pending:
                line, self._pending = self._pending.split(b"\n", 1)
                text = line.strip().decode("utf-8", "replace")
                if text.startswith("@"):
                    yield text[1:]
                elif text:
                    self.log.append(text)
        raise DeviceError(f"no reply within {timeout} s")

    def _reply(self, timeout):
        text = next(self._lines(timeout))
        reply = json.loads(text)
        if "error" in reply:
            raise DeviceError(reply["error"])
        return reply

    def command(self, line, timeout=3.0):
        self._serial.write((line + "\n").encode())
        return self._reply(timeout)

    def reboot(self, timeout=15.0):
        """Restarts the firmware and waits for the console: a clean state."""
        self.command("reboot")
        for text in self._lines(timeout):
            if json.loads(text).get("ready"):
                return

    # Input. Switch names: '1'..'7' chord buttons, 'J' joystick click,
    # 'K' key (gray), 'M' mode (red), 'S' sound (yellow).
    def press(self, switch):
        self.command(f"press {switch}")

    def release(self, switch):
        self.command(f"release {switch}")

    def tap(self, switch, hold=0.12):
        """Press and release, each held long enough to pass debounce."""
        self.press(switch)
        time.sleep(hold)
        self.release(switch)
        time.sleep(hold)

    def joystick(self, x, y):
        self.command(f"joy {x:.3f} {y:.3f}")

    def joystick_off(self):
        self.command("joy off")

    def flick(self, x, y, hold=0.1):
        """Push the stick and return it to center (one menu step)."""
        self.joystick(x, y)
        time.sleep(hold)
        self.joystick_off()
        time.sleep(hold)

    # Observation.
    def state(self):
        return self.command("state")

    def wait_for(self, predicate, timeout=3.0, interval=0.05):
        """Polls state until predicate(state) is true; returns that state."""
        deadline = time.monotonic() + timeout
        while True:
            current = self.state()
            if predicate(current):
                return current
            if time.monotonic() > deadline:
                raise DeviceError(f"condition not met within {timeout} s; last state: {current}")
            time.sleep(interval)

    def capture(self, source, ms):
        """Audio data lines arrive first, then the header as the reply."""
        self._serial.write(f"capture {source} {ms}\n".encode())
        data = bytearray()
        for text in self._lines(ms / 1000.0 + 30.0):
            if text.startswith("="):
                data += base64.b64decode(text[1:])
                continue
            header = json.loads(text)
            if "error" in header:
                raise DeviceError(header["error"])
            break
        samples = array.array("h")
        samples.frombytes(bytes(data))
        if len(samples) != header["frames"]:
            raise DeviceError(f"capture truncated: {len(samples)} of {header['frames']} frames")
        return Capture(header["capture"], header["rate"], header["first_frame"], samples)


def save_wav(capture, path):
    """Writes a capture as a mono 16-bit WAV for listening."""
    import wave
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(capture.rate)
        out.writeframes(capture.samples.tobytes())
