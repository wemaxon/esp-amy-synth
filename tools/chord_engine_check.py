#!/usr/bin/env python3
"""Compile and execute production chord/voice logic with a native C compiler."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="synth-chords-") as temporary:
        directory = Path(temporary)
        executable = directory / ("chords.exe" if os.name == "nt" else "chords")
        source = ROOT / "test/host/chord_harness.c"
        if os.name == "nt":
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            installation = subprocess.check_output([
                str(vswhere), "-latest", "-products", "*", "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
            ], text=True).strip()
            if not installation:
                raise RuntimeError("Install Visual Studio C++ Build Tools for native chord tests")
            setup = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
            compile_command = subprocess.list2cmdline([
                "cl", "/nologo", "/std:c11", "/O2", "/W3", "/WX", "/DCHORD_TEST_MAIN",
                f"/I{ROOT / 'main'}", str(source), f"/Fe:{executable}",
            ])
            subprocess.run(f'"{setup}" >nul && {compile_command}', shell=True, cwd=directory, check=True)
        else:
            compiler = os.environ.get("CC", "cc")
            if not shutil.which(compiler):
                raise RuntimeError("Set CC to a native C compiler for chord tests")
            subprocess.run([
                compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-DCHORD_TEST_MAIN", f"-I{ROOT / 'main'}", str(source), "-o", str(executable),
            ], cwd=directory, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
