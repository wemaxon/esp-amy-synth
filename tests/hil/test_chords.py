"""Chord engine on the device, against the HiChord Rev 2.9 manual."""

import time

from base import DeviceTestCase

# Root of each button in C major (MIDI).
ROOTS = {1: 60, 2: 62, 3: 64, 4: 65, 5: 67, 6: 69, 7: 71}


class ChordTest(DeviceTestCase):
    def play(self, button, joystick=None):
        """Hold a button (and optionally the stick); return the chord state."""
        if joystick is not None:
            self.device.joystick(*joystick)
        self.device.press(str(button))
        state = self.device.wait_for(lambda s: s["held"] == 1 << (button - 1) and s["notes"])
        if joystick is not None:
            state = self.device.wait_for(lambda s: s["modifier"] != "none")
        self.device.release(str(button))
        self.device.joystick_off()
        time.sleep(0.15)
        return state

    def test_plain_buttons(self):
        state = self.play(1)
        self.assertEqual((state["chord"], state["notes"]), ("C", [60, 64, 67]))

    def test_left_makes_major_and_minor_diminished_and_dim_minor(self):
        # Manual: "Major and minor chords become diminished. The diminished chord becomes minor."
        for button, chord, notes in ((1, "Cdim", [60, 63, 66]),
                                     (2, "Ddim", [62, 65, 68]),
                                     (7, "Bm", [71, 74, 78])):
            state = self.play(button, joystick=(-0.9, 0.0))
            self.assertEqual((state["chord"], state["notes"]), (chord, notes), f"button {button}")

    def test_ninth_on_diminished_button_is_half_diminished(self):
        # Manual: "On the diminished chord it plays m7b5."
        state = self.play(7, joystick=(0.7, -0.7))
        self.assertEqual((state["chord"], state["notes"]), ("Bm7b5", [71, 74, 77, 81]))

    def test_voice_leading_stays_in_the_button_register(self):
        self.toggle_setting("VOICE LEAD")
        self.assertTrue(self.device.state()["voice_leading"])
        circle_of_fifths = [1, 4, 7, 3, 6, 2, 5, 1]
        for _ in range(3):
            for button in circle_of_fifths:
                notes = self.play(button)["notes"]
                root = ROOTS[button]
                self.assertTrue(all(root <= note < root + 24 for note in notes),
                                f"button {button} drifted: {notes}")
