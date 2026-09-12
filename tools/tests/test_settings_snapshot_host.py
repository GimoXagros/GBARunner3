#!/usr/bin/env python3
"""Test boot report contents, numeric hash, escaping and truncation with mini-printf."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gbar3-snapshot-") as tmp:
    obj = Path(tmp) / "printf.o"
    exe = Path(tmp) / ("snapshot.exe" if os.name == "nt" else "snapshot")
    flags = ["-g", "-Wall", "-Wextra"]
    if os.environ.get("SANITIZE") == "1":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([os.environ.get("CC", "gcc"), *flags, "-c",
                    str(ROOT / "code/libs/mini-printf/mini-printf.c"), "-o", str(obj)], check=True)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", *flags,
                    "-I", str(ROOT / "tools/tests/host"),
                    "-I", str(ROOT / "code/core/arm9/source"),
                    "-I", str(ROOT / "code/libs/mini-printf"),
                    str(ROOT / "tools/tests/settings_snapshot_host.cpp"), str(obj), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: boot snapshot, real mini-printf, hash, escaping, truncation, no array mutation")
