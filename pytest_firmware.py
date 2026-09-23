# SPDX-FileCopyrightText: 2026 esp-amy-synth contributors
# SPDX-License-Identifier: MIT
"""Minimal on-device smoke checks for the actual firmware entrypoint.

The previous root test was copied from ESP-IDF's hello-world example and
asserted output that this firmware never emits.  Keep this test deliberately
small: detailed deterministic behavior is covered by the source-level parity
checks in ``tools/`` while this check proves that a flashed board reaches the
real application task.
"""

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.generic
@idf_parametrize("target", ["supported_targets"], indirect=["target"])
def test_firmware_reaches_application_task(dut: IdfDut) -> None:
    dut.expect("esp-pocket-audio synth app starting", timeout=20)

