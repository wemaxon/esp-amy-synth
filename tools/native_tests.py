"""Thin entry point shared by historical check commands; CTest owns execution."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "tests/native"


def run(test_filter: str | None = None) -> None:
    if sys.platform != "linux":
        raise SystemExit("Native tests require Linux. Run this command inside Ubuntu WSL2.")
    subprocess.run(["cmake", "--preset", "native-debug"], cwd=PROJECT, check=True, timeout=120)
    subprocess.run(["cmake", "--build", "--preset", "native-debug"], cwd=PROJECT, check=True, timeout=300)
    command = ["ctest", "--preset", "native-debug", "--output-junit",
               str(ROOT / "build/native-debug/results.xml")]
    if test_filter:
        command += ["-R", test_filter]
    subprocess.run(command, cwd=PROJECT, check=True, timeout=300)


if __name__ == "__main__":
    run()
