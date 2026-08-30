#!/usr/bin/env python3
"""Self-contained checks for the GBARunner3 diagnostic decoder."""

from __future__ import annotations

import csv
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


MAGIC = 0x47443347
HEADER = struct.Struct("<16I")
PREFIX = struct.Struct("<17I15H4H1H4I3I")
RECORD_SIZE = 216
CAPACITY = 256


def header(total: int, status: int = 1) -> bytes:
    return HEADER.pack(
        MAGIC, 2, HEADER.size, RECORD_SIZE, CAPACITY, total, total,
        int.from_bytes(b"B8CJ", "little"), 32 * 1024 * 1024, total, 0,
        status, 0, int(status == 3), 1 if status == 3 else 0xFFFFFFFF, 0,
    )


def run_decoder(decoder: Path, dump: Path, output: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(decoder), str(dump), "-o", str(output)],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    decoder = Path(__file__).with_name("decode_g3diag.py")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        armed = root / "armed.g3diag"
        armed_csv = root / "armed.csv"
        armed.write_bytes(header(0))
        result = run_decoder(decoder, armed, armed_csv)
        assert result.returncode == 0, result.stderr
        assert "status=armed" in result.stdout

        captured = root / "captured.g3diag"
        captured_csv = root / "captured.csv"
        ring = bytearray(RECORD_SIZE * CAPACITY)
        values = [0] * 44
        values[0] = 1
        values[36] = 0x03FB  # Select pressed (active low).
        PREFIX.pack_into(ring, 0, *values)
        captured.write_bytes(header(1, status=3) + ring)
        result = run_decoder(decoder, captured, captured_csv)
        assert result.returncode == 0, result.stderr
        assert "status=captured" in result.stdout
        with captured_csv.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        assert rows[0]["nds_keyinput"] == "0x000003FB"

        incomplete = root / "incomplete.g3diag"
        incomplete.write_bytes(header(1, status=2))
        result = run_decoder(decoder, incomplete, root / "incomplete.csv")
        assert result.returncode != 0
        assert "capture is incomplete" in result.stderr

    print("diagnostic decoder tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
