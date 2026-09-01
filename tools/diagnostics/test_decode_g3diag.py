#!/usr/bin/env python3
"""Self-contained checks for persistent runtime diagnostic decoding."""

from __future__ import annotations

import csv
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


MAGIC = 0x47443347
HEADER = struct.Struct("<16I")
PREFIX_V3 = struct.Struct("<17I21H4H1H4I3I4I")
DMA = struct.Struct("<IIHHII")
RECORD_SIZE_V3 = PREFIX_V3.size + 4 * DMA.size
CAPACITY_V3 = 64


def fnv1a(data: bytes, value: int = 2166136261) -> int:
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def legacy_ready_header() -> bytes:
    return HEADER.pack(
        MAGIC, 2, HEADER.size, 216, 256, 0, 0,
        int.from_bytes(b"B8CJ", "little"), 32 * 1024 * 1024,
        0, 0, 1, 0, 0, 0xFFFFFFFF, 0,
    )


def v3_checkpoint(sequence: int, sample: int, emulated_pc: int) -> bytes:
    ring = bytearray(RECORD_SIZE_V3 * CAPACITY_V3)
    values = [0] * 54
    values[0] = sample
    values[2] = emulated_pc
    values[13] = 100 + sample
    values[17] = 3  # DISPCNT mode 3
    values[19] = 160  # translated GBA VCOUNT during DS blanking
    values[42] = 0x03FF
    values[43] = 0x0080FFFF
    values[47] = 0x00000077
    values[50] = 0x00010003
    PREFIX_V3.pack_into(ring, 0, *values)
    header_values = [
        MAGIC, 3, HEADER.size, RECORD_SIZE_V3, CAPACITY_V3, 1, 1,
        int.from_bytes(b"B8CJ", "little"), 32 * 1024 * 1024,
        sequence, 0, 3, 0, 0, 0, 0,
    ]
    header = bytearray(HEADER.pack(*header_values))
    header_values[13] = fnv1a(ring, fnv1a(header))
    return HEADER.pack(*header_values) + ring


def run_decoder(decoder: Path, dumps: list[Path], output: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(decoder), *(str(path) for path in dumps), "-o", str(output)],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    decoder = Path(__file__).with_name("decode_g3diag.py")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        ready = root / "ready.g3diag"
        ready_csv = root / "ready.csv"
        ready.write_bytes(legacy_ready_header())
        result = run_decoder(decoder, [ready], ready_csv)
        assert result.returncode == 0, result.stderr
        assert "version=2" in result.stdout

        first = root / "game.g3diag.a"
        latest = root / "game.g3diag.b"
        output = root / "latest.csv"
        first.write_bytes(v3_checkpoint(7, 419, 0x0811FA94))
        latest.write_bytes(v3_checkpoint(8, 479, 0x0811F845))
        result = run_decoder(decoder, [first, latest], output)
        assert result.returncode == 0, result.stderr
        assert f"selected {latest}" in result.stdout
        assert "checksum=ok" in result.stdout
        with output.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        assert rows[0]["emulated_pc"] == "0x0811F845"
        assert rows[0]["dispcnt"] == "0x00000003"
        assert rows[0]["vcount"] == "0x000000A0"
        assert rows[0]["ds_dispcnt"] == "0x00010003"

        corrupt = root / "corrupt.g3diag.a"
        corrupt_data = bytearray(v3_checkpoint(9, 539, 0x0811FA94))
        corrupt_data[-1] ^= 0xFF
        corrupt.write_bytes(corrupt_data)
        result = run_decoder(decoder, [corrupt], root / "corrupt.csv")
        assert result.returncode != 0
        assert "checksum mismatch" in result.stdout

        incomplete = root / "incomplete.g3diag.a"
        incomplete.write_bytes(v3_checkpoint(10, 599, 0x0811FA94)[:-1])
        result = run_decoder(decoder, [incomplete], root / "incomplete.csv")
        assert result.returncode != 0
        assert "incomplete checkpoint" in result.stdout

    print("runtime diagnostic decoder tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
