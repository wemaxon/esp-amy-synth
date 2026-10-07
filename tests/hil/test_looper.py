"""Looper: musical lengths, continuous seam, layers, stick inhibit."""

import time

from analysis import largest_step
from base import DeviceTestCase

BAR_FRAMES_120 = 4 * 60 * 44100 // 120


def length_frames(state):
    return state["looper"]["length"]


class LooperTest(DeviceTestCase):
    def track(self, state, number):
        return state["looper"]["tracks"][number - 1]["state"]

    def record_first_loop(self, hold_chord):
        self.device.tap("J")  # open the looper
        self.device.tap("J")  # track 1 WAITING
        if hold_chord:
            self.device.press("1")
        self.device.tap("J")  # arm: count-in, then 4 bars
        return self.device.wait_for(lambda s: self.track(s, 1) == "PLAY", timeout=15)

    def test_first_loop_is_four_bars_with_a_continuous_seam(self):
        # A chord held through the whole take, including across the end.
        state = self.record_first_loop(hold_chord=True)
        time.sleep(0.5)  # past the take's delayed end (record latency)
        self.device.release("1")
        self.assertEqual(length_frames(state), 4 * BAR_FRAMES_120)

        # Start capturing 1.5 s before the next wrap so it sits mid-capture.
        anchor, length, rate = state["looper"]["anchor"], length_frames(state), state["looper"]["sample_rate"]
        now = self.device.state()["looper"]["frame"]
        next_wrap = anchor + ((now - anchor) // length + 1) * length
        if next_wrap - now < 2 * rate:  # too close to sleep 1.5 s before it
            next_wrap += length
        time.sleep((next_wrap - now) / rate - 1.5)
        capture = self.device.capture("output", 3000)
        self.save(capture, "seam")
        wrap = next_wrap - capture.first_frame
        count = len(capture.samples)
        self.assertTrue(4096 <= wrap <= count - 4096, f"wrap at {wrap} not inside the capture")
        at_seam = largest_step(capture.samples, wrap - 2048, wrap + 2048)
        elsewhere = max(largest_step(capture.samples, 1, wrap - 2048),
                        largest_step(capture.samples, wrap + 2048, count))
        self.assertLessEqual(at_seam, 1.5 * elsewhere + 64,
                             f"step at seam {at_seam} vs elsewhere {elsewhere}")

    def test_layer_stop_click_ends_at_the_loop(self):
        first = self.record_first_loop(hold_chord=False)
        self.device.tap("J")  # track 2 WAITING
        self.device.tap("J")  # armed: waits for the loop point
        self.device.wait_for(lambda s: self.track(s, 2) == "REC", timeout=12)
        self.device.tap("J")  # early stop click
        self.assertEqual(self.device.state()["looper"]["message"], "ENDS AT LOOP")
        state = self.device.wait_for(lambda s: self.track(s, 2) == "PLAY", timeout=12)
        self.assertEqual(state["looper"]["tracks"][1]["ms"], first["looper"]["tracks"][0]["ms"])

    def test_stick_does_not_navigate_while_recording(self):
        self.device.tap("J")
        self.device.tap("J")
        before = self.device.state()["looper"]
        self.device.tap("J")  # count-in
        self.device.flick(0.9, 0.0)
        self.device.wait_for(lambda s: self.track(s, 1) == "REC", timeout=5)
        self.device.flick(0.9, 0.0)
        after = self.device.state()["looper"]
        self.assertEqual((after["current"], after["bars"]), (before["current"], before["bars"]))
