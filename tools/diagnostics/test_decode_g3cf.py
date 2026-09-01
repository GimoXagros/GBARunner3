#!/usr/bin/env python3
"""Controlled fixtures for the persistent control-flow decoder."""

from __future__ import annotations

import struct
import tempfile
from pathlib import Path

from decode_g3cf import EVENT, HEADER, MAGIC, fnv1a, read_checkpoint


CAPACITY = 128


def make_checkpoint(path: Path, checkpoint_sequence: int, corrupt: bool = False) -> None:
    ring = bytearray(EVENT.size * CAPACITY)
    event = [0] * 24
    event[0] = 7
    event[1] = 12
    event[2] = 0x09ED4000
    event[8] = 0x03000001
    EVENT.pack_into(ring, 0, *event)
    header = [
        MAGIC, 1, HEADER.size, EVENT.size, CAPACITY, 1, 1,
        int.from_bytes(b"B8CJ", "little"), 32 * 1024 * 1024,
        checkpoint_sequence, 3, 0, 0, 4, 0, 1,
    ]
    header_bytes = bytearray(HEADER.pack(*header))
    header[12] = fnv1a(ring, fnv1a(header_bytes))
    data = bytearray(HEADER.pack(*header) + ring)
    if corrupt:
        data[-1] ^= 1
    path.write_bytes(data)


def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        first = root / "first.g3diag.a"
        second = root / "second.g3diag.b"
        bad = root / "bad.g3diag.a"
        make_checkpoint(first, 3)
        make_checkpoint(second, 4)
        make_checkpoint(bad, 5, corrupt=True)

        assert read_checkpoint(first).checkpoint_sequence == 3
        selected = max((read_checkpoint(first), read_checkpoint(second)), key=lambda item: item.checkpoint_sequence)
        assert selected.path == second
        try:
            read_checkpoint(bad)
        except ValueError as error:
            assert "checksum mismatch" in str(error)
        else:
            raise AssertionError("corrupt checkpoint was accepted")

    print("control-flow decoder tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
