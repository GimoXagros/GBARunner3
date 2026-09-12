#!/usr/bin/env python3
"""Ensure an ARM BL->B mutation cannot pass the linked relocation audit."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from elftools.elf.elffile import ELFFile

path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
with path.open("rb") as stream:
    elf = ELFFile(stream)
    function = elf.get_section_by_name(".symtab").get_symbol_by_name("emu_regDispCntStore")[0]
    section = elf.get_section(function["st_shndx"])
    file_start = section["sh_offset"] + function["st_value"] - section["sh_addr"]
    calls = [offset for offset in range(0, function["st_size"], 4)
             if struct.unpack_from("<I", data, file_start + offset)[0] & 0xFF000000 == 0xEB000000]
    assert calls, "expected ARM BL to displayModeChange"

with tempfile.TemporaryDirectory(prefix="gbar3-audit-negative-") as tmp:
    mutated = Path(tmp) / "mutated.elf"
    output = Path(tmp) / "audit.json"
    script = Path(__file__).resolve().parents[1] / "audit_display_elf.py"
    for invert_link in (False, True):
        candidate = bytearray(data)
        if invert_link:
            # Branch displacement and condition remain identical; only link is removed.
            candidate[file_start + calls[0] + 3] ^= 1
        mutated.write_bytes(candidate)
        subprocess.run([sys.executable, str(script), str(path), str(mutated), "--output", str(output)],
                       check=True, stdout=subprocess.DEVNULL)
        rows = json.loads(output.read_text())["functions"]
        row = next(r for r in rows if r["symbol"] == "emu_regDispCntStore")
        assert row["classification"] == ("unexpected difference" if invert_link else "byte-identical"), row
print("PASS: identical ELF accepted; same-target BL-to-B mutation rejected")
