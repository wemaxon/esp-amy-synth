#!/usr/bin/env python3
"""Run chord behavior tests through the shared Linux-native build."""
from native_tests import run

def main() -> None:
    run("^chord$")

if __name__ == "__main__":
    main()
