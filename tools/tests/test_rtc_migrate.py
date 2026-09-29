#!/usr/bin/env python3
"""Black-box consent and byte-preservation checks for synthetic RTC migration.

No user ROM, save, or RTC data is opened. The ROM-like input here contains only
deterministic header bytes and has no executable game content.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from rtc_legacy_fixtures import fnv1a, legacy_record_from_old_clock, modern_record, sha256

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rtc_migrate.py"


def synthetic_rom(path: Path, code: bytes = b"RTCA") -> tuple[int, int, int]:
    header = bytearray((i * 37 + 11) & 255 for i in range(192))
    header[0xAC:0xB0] = code
    image = bytes(header) + bytes(range(64))
    path.write_bytes(image)
    return struct.unpack("<I", code)[0], len(image), fnv1a(header)


def check_pending(data: bytes, identity: tuple[int, int, int],
                  originals: tuple[bytes, bytes, bytes]) -> None:
    assert len(data) == 200, "pending exact 200-byte size"
    assert struct.unpack_from("<IHH", data) == (0x32523347, 2, 188), "pending format header"
    assert struct.unpack_from("<III", data, 8) == identity, "pending ROM identity"
    assert struct.unpack_from("<II", data, 20) == (0, 0), "pending sequence and unanchored DS time"
    assert struct.unpack_from("<hHHH", data, 32) == (-2, 0x40, 0x1234, 0), "pending RTC metadata"
    assert struct.unpack_from("<HBBBB", data, 40) == (1, 0, 7, 1, 0), "pending consent policy/lineage"
    assert data[45:48] == b"\0\0\0", "pending reserved bytes"
    assert struct.unpack_from("<I", data, 48)[0] == 8, "pending selected v1 sequence"
    assert struct.unpack_from("<III", data, 52) == tuple(map(fnv1a, originals)), "pending source hashes"
    assert data[64:196] == b"".join(originals), "pending embeds exact v1 bytes"
    assert struct.unpack_from("<I", data, 196)[0] == fnv1a(data[:196]), "pending checksum"
    assert struct.unpack_from("<I", data, 28)[0] == struct.unpack_from("<I", originals[1], 28)[0], "pending stored game snapshot"


def reference_pending(identity: tuple[int, int, int],
                      originals: tuple[bytes, bytes, bytes]) -> bytes:
    """Contract-only byte oracle, independent of the migration implementation."""
    return modern_record(identity, sequence=0, host_seconds=0,
                         game_seconds=struct.unpack_from("<I", originals[1], 28)[0],
                         legacy=originals, selected_role=1, pending=True)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="gbar3-rtc-migrate-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.rom = self.root / "synthetic.gba"
        self.identity = synthetic_rom(self.rom)
        self.paths = (self.root / "synthetic.g3rtc", self.root / "synthetic.g3rtc.tmp",
                      self.root / "synthetic.g3rtc.bak")
        clock = datetime(2024, 2, 28, 22, 45, 45)
        self.originals = tuple(legacy_record_from_old_clock(
            self.identity, sequence=seq, host=clock, game=clock,
        ) for seq in (7, 8, 6))
        for path, data in zip(self.paths, self.originals):
            path.write_bytes(data)
        self.save = self.root / "synthetic.sav"
        self.save.write_bytes(bytes((i * 19) & 255 for i in range(2048)))
        self.output = self.root / "synthetic.g3rtc2"

    def cmd(self, action: str | None = None, *extra: str) -> subprocess.CompletedProcess[str]:
        arguments = [sys.executable, str(TOOL)]
        if action:
            arguments.append(action)
        arguments += ["--rom", str(self.rom),
                      "--legacy-primary", str(self.paths[0]),
                      "--legacy-temp", str(self.paths[1]),
                      "--legacy-backup", str(self.paths[2]), *extra]
        return subprocess.run(arguments, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=15)

    def snapshot(self) -> dict[str, str]:
        return {p.name: sha256(p.read_bytes()) for p in self.root.iterdir() if p.is_file()}

    def adopt(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return self.cmd("adopt-stored-snapshot", "--source", "temp",
                        "--expected-sha256", sha256(self.originals[1]),
                        "--output", str(self.output), *extra)

    def test_inspect_default_and_explicit_inspect_write_zero_bytes(self):
        before = self.snapshot()
        for action in (None, "inspect"):
            result = self.cmd(action)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.snapshot(), before, "inspect must make zero filesystem writes")
            report = (result.stdout + result.stderr).lower()
            for role in ("primary", "temp", "backup"):
                self.assertIn(role, report, f"inspect reports {role}")

    def test_explicit_adoption_preserves_all_sources_and_save(self):
        before = self.snapshot()
        result = self.adopt()
        self.assertEqual(result.returncode, 0, result.stderr)
        check_pending(self.output.read_bytes(), self.identity, self.originals)
        self.assertEqual(self.output.read_bytes(), reference_pending(self.identity, self.originals),
                         "pending bytes match independent contract serializer")
        after = self.snapshot()
        self.assertEqual({key: after[key] for key in before}, before,
                         "adoption preserves v1 triad, ROM and save byte-for-byte")

    def test_bad_consent_inputs_and_output_collision_write_nothing(self):
        pristine = self.snapshot()
        bad_commands = [
            self.cmd("adopt-stored-snapshot", "--source", "temp", "--output", str(self.output)),
            self.cmd("adopt-stored-snapshot", "--source", "temp", "--expected-sha256", "0" * 64,
                     "--output", str(self.output)),
            self.cmd("adopt-stored-snapshot", "--source", "temp",
                     "--expected-sha256", sha256(self.originals[1]),
                     "--output", str(self.paths[0])),
        ]
        for result in bad_commands:
            self.assertNotEqual(result.returncode, 0, "invalid consent must reject")
        self.assertFalse(self.output.exists(), "invalid consent must not create output")
        self.assertEqual(self.snapshot(), pristine, "invalid consent preserves every input byte")
        before = self.snapshot()
        self.assertEqual(self.adopt().returncode, 0)
        output_hash = sha256(self.output.read_bytes())
        duplicate = self.adopt()
        self.assertNotEqual(duplicate.returncode, 0, "existing output must reject")
        self.assertEqual(sha256(self.output.read_bytes()), output_hash, "existing output unchanged")
        for name, old_hash in before.items():
            self.assertEqual(sha256((self.root / name).read_bytes()), old_hash,
                             f"{name} remains unchanged")

    def test_other_rom_identity_and_malformed_sibling_reject(self):
        foreign = self.root / "foreign.gba"
        synthetic_rom(foreign, b"RTCB")
        self.rom = foreign
        self.assertNotEqual(self.adopt().returncode, 0, "other ROM identity must reject")
        self.assertFalse(self.output.exists())
        self.rom = self.root / "synthetic.gba"
        self.paths[2].write_bytes(self.originals[2][:-1])
        self.assertNotEqual(self.adopt().returncode, 0, "malformed present backup must reject")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--negative-control", choices=["bad-pending-checksum"])
    args, remaining = parser.parse_known_args()
    if args.negative_control:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rom.gba"
            identity = synthetic_rom(path)
            originals = tuple(legacy_record_from_old_clock(
                identity, sequence=seq, host=datetime(2024, 2, 28, 22, 45, 45),
                game=datetime(2024, 2, 28, 22, 45, 45),
            ) for seq in (7, 8, 6))
            data = bytearray(reference_pending(identity, originals))
            data[196] ^= 1
            try:
                check_pending(bytes(data), identity, originals)
            except AssertionError as error:
                assert str(error) == "pending checksum", "wrong negative-control assertion"
                print("PASS negative control rejected bad pending checksum")
            else:
                raise AssertionError("negative control escaped")
    else:
        unittest.main(argv=[sys.argv[0], *remaining])
