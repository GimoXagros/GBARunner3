#!/usr/bin/env python3
"""Execute the real C++ serializer with host platform shims and all shipped configs."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix="gbar3-json-") as tmp:
        exe = Path(tmp) / ("json.exe" if os.name == "nt" else "json")
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-g",
                   "-I", str(ROOT / "tools/tests/host"), "-I", str(ROOT / "code/core/arm9/source"),
                   str(ROOT / "tools/tests/host/json_settings.cpp"), "-o", str(exe)]
        if os.environ.get("SANITIZE") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
        count = 0
        paths = sorted((ROOT / "configs").glob("*.json"))
        for path in paths:
            settings = json.loads(path.read_text()).get("runSettings", {})
            expected = [int(v, 16) for key in ("jitPatchAddresses", "selfModifyingPatchAddresses") for v in settings.get(key, [])]
            copied = Path(tmp) / path.name
            shutil.copyfile(path, copied)
            actual = subprocess.check_output([str(exe), str(copied)], text=True)
            assert list(map(int, actual.split())) == expected, path
            count += len(expected)
        assert len(paths) == 304 and count == 2513, (len(paths), count)
        print(f"PASS: {len(paths)} configs, {count} unchanged addresses")

if __name__ == "__main__":
    main()
