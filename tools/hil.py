#!/usr/bin/env python3
"""Hardware-in-the-loop tests on the ESP32-P4 board.

Builds the test firmware (sdkconfig.test: test console on USB-Serial-JTAG),
flashes it, and runs the scenarios in tests/hil against the device. Run from
an ESP-IDF shell (idf.py and pyserial on the path). The serial port must be
free: close any idf.py monitor first.

  python tools/hil.py                     build, flash, run everything
  python tools/hil.py --no-flash          reuse the firmware on the device
  python tools/hil.py test_chords         run one module (or Module.Class.test)
"""

import argparse
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "p4-test"
CONFIG = [
    "-B", str(BUILD),
    "-D", f"SDKCONFIG={BUILD / 'sdkconfig'}",
    "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.test",
    "-D", "IDF_TARGET=esp32p4",
]


def idf(*args):
    subprocess.run(["idf.py", *CONFIG, *args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tests", nargs="*", help="test modules or names (default: all)")
    parser.add_argument("--port", default=os.environ.get("HIL_PORT", "/dev/ttyACM0"))
    parser.add_argument("--no-flash", action="store_true", help="skip build and flash")
    args = parser.parse_args()

    if not args.no_flash:
        idf("build")
        idf("-p", args.port, "flash")

    os.environ["HIL_PORT"] = args.port
    os.environ.setdefault("HIL_ARTIFACTS", str(ROOT / "build" / "hil"))
    tests_dir = ROOT / "tests" / "hil"
    sys.path.insert(0, str(tests_dir))
    loader = unittest.TestLoader()
    suite = (loader.loadTestsFromNames(args.tests) if args.tests
             else loader.discover(str(tests_dir), top_level_dir=str(tests_dir)))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
