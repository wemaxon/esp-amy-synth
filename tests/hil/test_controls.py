"""Input handling: shortcut timing, menu steps, stability."""

import time

from base import DeviceTestCase


class ControlsTest(DeviceTestCase):
    def test_boots_into_play_with_stack_headroom(self):
        state = self.device.state()
        self.assertEqual(state["mode"], "PLAY ONESHOT")
        self.assertGreater(state["app_stack_free"], 2048)

    def test_red_after_holding_a_chord_raises_its_octave(self):
        self.device.press("1")
        time.sleep(0.3)
        self.device.tap("M")
        self.device.release("1")
        state = self.device.wait_for(lambda s: s["button_octaves"][0] == 1)
        self.assertEqual(state["mode"], "PLAY ONESHOT")

    def test_red_pressed_with_a_chord_is_the_mode_combo(self):
        # Chord and Red within one scan: quick-select mode 2 (Strum), no octave.
        self.device.press("2")
        self.device.press("M")
        time.sleep(0.15)
        self.device.release("M")
        self.device.release("2")
        state = self.device.wait_for(lambda s: s["mode"] == "STRUM")
        self.assertEqual(state["button_octaves"][1], 0)

    def test_menu_steps_once_per_push(self):
        self.device.press("K")
        for x, y in ((0.0, 0.9), (0.6, 0.75), (0.0, 0.9), (-0.6, 0.75), (0.0, 0.9)):
            self.device.joystick(x, y)  # wobble around Up without returning to center
            time.sleep(0.08)
        self.device.joystick_off()
        time.sleep(0.1)
        self.device.release("K")
        self.assertEqual(self.device.wait_for(lambda s: True)["octave"], 1)

    def test_cycling_every_sound_keeps_stack_headroom(self):
        self.device.press("S")
        for _ in range(24):
            self.device.flick(0.9, 0.0)
        self.device.release("S")
        time.sleep(0.5)
        self.device.press("1")  # play on whatever sound it ended on
        time.sleep(0.3)
        self.device.release("1")
        self.assertGreater(self.device.state()["app_stack_free"], 2048)
