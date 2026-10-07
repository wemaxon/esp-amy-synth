"""Shared device connection and navigation helpers for hardware tests."""

import os
import time
import unittest
from pathlib import Path

from device import Device, save_wav

ARTIFACTS = Path(os.environ.get("HIL_ARTIFACTS", "build/hil"))
_device = None


def shared_device():
    global _device
    if _device is None:
        _device = Device()
    return _device


class DeviceTestCase(unittest.TestCase):
    """Each test starts from a freshly booted firmware."""

    device = None

    def setUp(self):
        self.device = shared_device()
        self.device.reboot()

    def tearDown(self):
        # Never leave virtual input behind for the next test.
        for switch in ("1", "2", "3", "4", "5", "6", "7", "J", "K", "M", "S"):
            self.device.release(switch)
        self.device.joystick_off()
        time.sleep(0.1)

    def save(self, capture, name):
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        save_wav(capture, ARTIFACTS / f"{self.id().split('.')[-1]}-{name}.wav")

    def enter_mode(self, name):
        """Mode menu (hold red), joystick right until the mode is reached."""
        self.device.press("M")
        try:
            for _ in range(20):
                if self.device.state()["mode"] == name:
                    return
                self.device.flick(0.9, 0.0)
            self.fail(f"mode {name} not reached")
        finally:
            self.device.release("M")
            time.sleep(0.15)

    def toggle_setting(self, name):
        """Sound menu (hold yellow): joystick down to the row, click to change."""
        self.device.press("S")
        try:
            for _ in range(20):
                if self.device.state()["effect"] == name:
                    self.device.tap("J")
                    return
                self.device.flick(0.0, -0.9)
            self.fail(f"setting {name} not reached")
        finally:
            self.device.release("S")
            time.sleep(0.15)
