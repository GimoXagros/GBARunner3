#!/usr/bin/env python3
"""Compile the exact searchHiCode function with a hostile synthetic SD cache."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SAVE = ROOT / "code/core/arm9/source/Save"
with tempfile.TemporaryDirectory(prefix="gbar3-search-") as tmp:
    tmp = Path(tmp)
    source = (SAVE / "Save.cpp").read_text(encoding="utf-8")
    start = source.index("static u32* searchHiCode(")
    end = source.index("\n#endif", start)
    (tmp / "production_search.h").write_text(source[start:end], encoding="utf-8")
    signatures = []
    for path in (SAVE / "SaveEeprom.cpp", SAVE / "SaveFlash.cpp", SAVE / "SaveSram.cpp"):
        signatures += re.findall(r"(?:static\s+)?const u32\s+(\w+)\[(?:4)?\]\s*=\s*\{([^}]+)\}", path.read_text())
    assert signatures, "no source signature corpus found"
    (tmp / "signatures.h").write_text("const u32 corpus[][4] = {\n" + ",\n".join("{" + values + "}" for _, values in signatures) + "\n};\n")
    exe = tmp / ("search.exe" if os.name == "nt" else "search")
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-Wall", "-Wextra", "-fpermissive",
               "-I", str(tmp), "-I", str(ROOT / "code/core/arm9/source"),
               str(ROOT / "tools/tests/save_search_host.cpp"), "-o", str(exe)]
    if os.environ.get("SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
    print(f"PASS: {len(signatures)} existing EEPROM/FLASH/FLASH512/FLASH1M/SRAM signatures")
