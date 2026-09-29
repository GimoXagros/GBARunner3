#!/usr/bin/env python3
"""Inspect legacy GBARunner3 RTC files or explicitly adopt a stored snapshot.

The tool never estimates an old wall clock or uses the PC clock. Adoption writes
only a new, unanchored pending record; the DS loader supplies its own RTC anchor.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import sys

LEGACY_MAGIC = 0x54523347
MODERN_MAGIC = 0x32523347
RTC_CYCLE_SECONDS = 36525 * 86400
ROLES = ("primary", "temp", "backup")


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rom_identity(path: Path) -> tuple[int, int, int]:
    with path.open("rb") as stream:
        header = stream.read(192)
        stream.seek(0, os.SEEK_END)
        size = stream.tell()
    if len(header) != 192 or size > 0xFFFFFFFF:
        raise ValueError("ROM must contain a complete 192-byte header and fit the RTC size field")
    return struct.unpack_from("<I", header, 0xAC)[0], size, fnv1a(header)


def inspect_legacy(path: Path, identity: tuple[int, int, int]) -> tuple[str, bytes | None]:
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        return "MISSING", None
    except OSError as error:
        return f"IO_ERROR ({error})", None
    digest = sha256(raw)
    if len(raw) < 8:
        return f"CORRUPT size={len(raw)} sha256={digest}", raw
    magic, version = struct.unpack_from("<IH", raw)
    if magic == LEGACY_MAGIC and version != 1:
        return f"UNSUPPORTED_VERSION version={version} sha256={digest}", raw
    if len(raw) != 44 or magic != LEGACY_MAGIC:
        return f"CORRUPT size={len(raw)} sha256={digest}", raw
    fields = struct.unpack("<IHHIIIIIIhHHHI", raw)
    _, _, length, game_code, rom_size, header_hash, sequence, host, game, week, status, _interrupt, flags, checksum = fields
    if length != 32 or flags != 0 or checksum != fnv1a(raw[:40]) or \
            host >= RTC_CYCLE_SECONDS or game >= RTC_CYCLE_SECONDS or \
            not -6 <= week <= 6 or status & ~0xEA:
        return f"CORRUPT sha256={digest}", raw
    if (game_code, rom_size, header_hash) != identity:
        return f"IDENTITY_MISMATCH sha256={digest}", raw
    return f"VALID_LEGACY sequence={sequence} game_seconds={game} sha256={digest}", raw


def pending_record(identity: tuple[int, int, int], sources: tuple[bytes | None, ...],
                   selected_role: int) -> bytes:
    selected = sources[selected_role]
    assert selected is not None
    _, _, _, _, _, _, sequence, _, game, week, status, interrupt, _, _ = \
        struct.unpack("<IHHIIIIIIhHHHI", selected)
    record = bytearray(200)
    struct.pack_into("<IHH", record, 0, MODERN_MAGIC, 2, 188)
    struct.pack_into("<III", record, 8, *identity)
    struct.pack_into("<III", record, 20, 0, 0, game)
    struct.pack_into("<hHHH", record, 32, week, status, interrupt, 0)
    presence = sum(1 << role for role, raw in enumerate(sources) if raw is not None)
    struct.pack_into("<HBBB", record, 40, 1, 0, presence, selected_role)
    struct.pack_into("<I", record, 48, sequence)
    for role, raw in enumerate(sources):
        if raw is None:
            continue
        struct.pack_into("<I", record, 52 + role * 4, fnv1a(raw))
        record[64 + role * 44:64 + (role + 1) * 44] = raw
    struct.pack_into("<I", record, 196, fnv1a(record[:196]))
    return bytes(record)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", choices=("inspect", "adopt-stored-snapshot"),
                        default="inspect", help="default is read-only inspect")
    parser.add_argument("--rom", type=Path, required=True, help="ROM identity source, read-only")
    parser.add_argument("--legacy-primary", type=Path, required=True)
    parser.add_argument("--legacy-temp", type=Path, required=True)
    parser.add_argument("--legacy-backup", type=Path, required=True)
    parser.add_argument("--source", choices=ROLES, help="legacy snapshot to adopt")
    parser.add_argument("--expected-sha256", help="SHA256 shown by inspect for chosen source")
    parser.add_argument("--output", type=Path, help="separate new .g3rtc2 pending output")
    args = parser.parse_args(argv)
    paths = (args.legacy_primary, args.legacy_temp, args.legacy_backup)
    try:
        identity = rom_identity(args.rom)
        inspected = tuple(inspect_legacy(path, identity) for path in paths)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    for role, path, (status, _) in zip(ROLES, paths, inspected):
        print(f"{role}: {path}: {status}")
    print(f"ROM identity: gameCode={identity[0]:08x} romSize={identity[1]} headerFnv1a={identity[2]:08x}")
    print("Stored seconds are a snapshot; original wall time and past offline elapsed are unknown.")
    if args.action == "inspect":
        return 0

    if args.source is None or args.expected_sha256 is None or args.output is None:
        print("ERROR: adoption requires --source, --expected-sha256, and --output", file=sys.stderr)
        return 2
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_sha256):
        print("ERROR: expected SHA256 must be 64 hexadecimal characters", file=sys.stderr)
        return 2
    if any(not status.startswith("VALID_LEGACY") and status != "MISSING"
           for status, _ in inspected):
        print("ERROR: every existing legacy sibling must validate before adoption", file=sys.stderr)
        return 2
    selected_role = ROLES.index(args.source)
    selected = inspected[selected_role][1]
    if selected is None:
        print("ERROR: selected legacy source is missing", file=sys.stderr)
        return 2
    if sha256(selected).lower() != args.expected_sha256.lower():
        print("ERROR: selected source SHA256 changed or does not match consent", file=sys.stderr)
        return 2
    output_resolved = args.output.resolve(strict=False)
    source_paths = (args.rom, *paths)
    if any(output_resolved == path.resolve(strict=False) for path in source_paths):
        print("ERROR: output must be separate from all input files", file=sys.stderr)
        return 2
    if args.output.exists() or args.output.is_symlink():
        print("ERROR: output already exists; no file was replaced", file=sys.stderr)
        return 2
    # Recheck input bytes immediately before creating output. The target repeats
    # the exact-byte lineage check at every boot and write.
    for path, (_, previous) in zip(paths, inspected):
        try:
            current = path.read_bytes()
        except FileNotFoundError:
            current = None
        except OSError as error:
            print(f"ERROR: source reread failed: {error}", file=sys.stderr)
            return 2
        if current != previous:
            print("ERROR: a legacy source changed during inspection", file=sys.stderr)
            return 2
    data = pending_record(identity, tuple(raw for _, raw in inspected), selected_role)
    try:
        with args.output.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        print("ERROR: output appeared during adoption; no file was replaced", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"ERROR: output write failed: {error}", file=sys.stderr)
        return 2
    print(f"Pending snapshot created: {args.output} sha256={sha256(data)}")
    print("Copy it to the matching ROM .g3rtc2 sidecar path. On DS boot, the loader checks the")
    print("unchanged legacy files and anchors to the DS RTC before starting the game.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
