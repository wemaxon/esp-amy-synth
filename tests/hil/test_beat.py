"""Drum loop: sample-accurate timing, own bus."""

import time

from analysis import onsets, peak
from base import DeviceTestCase


def step_frame(origin, bpm, rate, step):
    """Same rational timing as synth_transport_step_offset (sixteenths)."""
    return origin + (step * 60 * rate) // (bpm * 4)


class BeatTest(DeviceTestCase):
    def setUp(self):
        super().setUp()
        self.enter_mode("DRUM LOOP")
        self.device.tap("1")  # Rock
        self.beat = self.device.wait_for(lambda s: s["beat"]["running"])["beat"]

    def test_hits_land_on_their_steps(self):
        capture = self.device.capture("output", 3000)
        self.save(capture, "beat")
        origin, bpm, rate = self.beat["origin"], self.beat["bpm"], capture.rate
        first = capture.first_frame
        # Rock plays every eighth (hats), so every even step has an attack.
        expected = []
        step = 0
        while step_frame(origin, bpm, rate, step) < first + len(capture.samples):
            frame = step_frame(origin, bpm, rate, step)
            if frame >= first + 64 and step % 2 == 0:
                expected.append(frame - first)
            step += 1
        found = onsets(capture.samples)
        self.assertGreaterEqual(len(expected), 10)
        for position in expected:
            nearest = min(found, key=lambda onset: abs(onset - position))
            # One onset-detector window (32 samples, 0.7 ms) of tolerance.
            self.assertLessEqual(abs(nearest - position), 32,
                                 f"hit at {position} detected at {nearest}")

    def test_beat_is_not_on_the_synth_bus(self):
        capture = self.device.capture("synth", 1000)
        self.assertLess(peak(capture.samples), 64)

    def tearDown(self):
        self.device.tap("1")  # stop the beat
        super().tearDown()
