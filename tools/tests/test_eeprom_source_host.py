#!/usr/bin/env python3
"""Execute the existing EEPROM version selectors and byte wrappers, not a ROM."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gbar3-eeprom-") as tmp:
    tmp = Path(tmp)
    source = (ROOT / "code/core/arm9/source/Save/SaveEeprom.cpp").read_text(encoding="utf-8")
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    (tmp / "production_eeprom.h").write_text(source, encoding="utf-8")
    exe = tmp / ("eeprom.exe" if os.name == "nt" else "eeprom")
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-Wall", "-I", str(tmp),
               str(ROOT / "tools/tests/eeprom_source_host.cpp"), "-o", str(exe)]
    if os.environ.get("SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
    scanner = (ROOT / "code/core/arm9/source/Save/SaveTagScanner.cpp").read_text()
    for version in (124, 125):
        assert re.search(r'\{"EEPROM_V' + str(version) + r'"[^\n]+8 \* 1024, eeprom_patchV124\}', scanner)
    print("PASS: V124 and V125 select the same V124 patcher and 8 KiB size")
