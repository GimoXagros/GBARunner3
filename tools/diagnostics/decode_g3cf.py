#!/usr/bin/env python3
"""Decode persistent GBARunner3 control-flow breadcrumb checkpoints."""

from __future__ import annotations

import argparse
import csv
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x46433347
HEADER = struct.Struct("<16I")
EVENT = struct.Struct("<24I")

EVENT_NAMES = {
    1: "DIAGNOSTIC_ARM",
    2: "INPUT",
    3: "VBLANK",
    10: "ARM_B",
    11: "ARM_BL",
    12: "ARM_BX",
    13: "ARM_LDR_PC",
    14: "ARM_LDM_PC",
    15: "ARM_ALU_PC",
    20: "THUMB_B",
    21: "THUMB_BCOND",
    22: "THUMB_BL_PREFIX",
    23: "THUMB_BL",
    24: "THUMB_BX",
    25: "THUMB_MOV_PC",
    26: "THUMB_ADD_PC",
    27: "THUMB_POP_PC",
    30: "SWI",
    31: "UNDEFINED",
    32: "PREFETCH_ABORT",
    33: "HICODE_MISS",
    34: "HICODE_MAP",
    35: "SDCACHE_LOAD",
    36: "SDCACHE_EVICT",
    37: "NOT_IMPLEMENTED",
}

STATUS_NAMES = {1: "ready", 2: "armed", 3: "checkpoint", 4: "emergency"}
REASON_NAMES = {
    1: "boot",
    2: "arm",
    3: "input",
    4: "periodic",
    5: "not-implemented",
    6: "repeated-prefetch-abort",
    7: "repeated-hicode-miss",
}

COLUMNS = (
    "sequence", "type", "source_guest_pc", "source_execution_pc", "instruction",
    "state", "cpsr", "lr", "raw_target", "normalized_guest_target",
    "final_execution_target", "source_rom_block", "target_rom_block",
    "source_cache_block", "target_cache_block", "jit_state", "hicode_block",
    "hicode_mask", "mpu_region4", "prefetch_abort_count", "undefined_count",
    "hicode_miss_count", "sdcache_load_count", "aux",
)


@dataclass(frozen=True)
class Checkpoint:
    path: Path
    header: tuple[int, ...]
    rows: list[tuple[int, ...]]

    @property
    def checkpoint_sequence(self) -> int:
        return self.header[9]


def fnv1a(data: bytes, value: int = 2166136261) -> int:
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def read_checkpoint(path: Path) -> Checkpoint:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"{path}: shorter than the control-flow header")
    header = HEADER.unpack_from(data)
    magic, version, header_size, record_size, capacity, write_index, total = header[:7]
    if magic != MAGIC:
        raise ValueError(f"{path}: not a G3CF checkpoint")
    if version != 1 or header_size != HEADER.size or record_size != EVENT.size:
        raise ValueError(f"{path}: unsupported G3CF layout")
    if capacity == 0 or capacity & (capacity - 1) or write_index >= capacity:
        raise ValueError(f"{path}: invalid ring geometry")

    if total == 0:
        if len(data) != HEADER.size:
            raise ValueError(f"{path}: ready marker has trailing data")
        return Checkpoint(path, header, [])

    expected_size = header_size + capacity * record_size
    if len(data) != expected_size:
        raise ValueError(f"{path}: incomplete checkpoint ({len(data)} != {expected_size})")

    checksum_header = bytearray(data[:header_size])
    struct.pack_into("<I", checksum_header, 12 * 4, 0)
    checksum = fnv1a(data[header_size:], fnv1a(checksum_header))
    if checksum != header[12]:
        raise ValueError(
            f"{path}: checksum mismatch (0x{checksum:08X} != 0x{header[12]:08X})"
        )

    present = min(total, capacity)
    oldest = write_index if total >= capacity else 0
    rows = []
    for logical in range(present):
        physical = (oldest + logical) % capacity
        rows.append(EVENT.unpack_from(data, header_size + physical * record_size))
    return Checkpoint(path, header, rows)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("dumps", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checkpoints = []
    for path in args.dumps:
        try:
            checkpoint = read_checkpoint(path)
        except ValueError as error:
            print(error)
            continue
        status = STATUS_NAMES.get(checkpoint.header[10], f"unknown-{checkpoint.header[10]}")
        reason = REASON_NAMES.get(checkpoint.header[13], f"unknown-{checkpoint.header[13]}")
        print(
            f"{path}: sequence={checkpoint.checkpoint_sequence} status={status} "
            f"reason={reason} events={len(checkpoint.rows)} checksum=ok"
        )
        checkpoints.append(checkpoint)

    if not checkpoints:
        raise SystemExit("no valid G3CF checkpoint")
    checkpoint = max(checkpoints, key=lambda item: item.checkpoint_sequence)
    game_code = checkpoint.header[7].to_bytes(4, "little").decode("ascii", errors="replace")
    output = args.output or checkpoint.path.with_suffix(".csv")
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(COLUMNS)
        for row in checkpoint.rows:
            writer.writerow(
                [f"0x{row[0]:08X}", EVENT_NAMES.get(row[1], f"EVENT_{row[1]}")]
                + [f"0x{value:08X}" for value in row[2:]]
            )

    print(
        f"selected {checkpoint.path} sequence={checkpoint.checkpoint_sequence} "
        f"for {game_code}; wrote {len(checkpoint.rows)} events to {output}"
    )
    for row in checkpoint.rows[-12:]:
        print(
            f"#{row[0]:08X} {EVENT_NAMES.get(row[1], str(row[1])):<18} "
            f"src={row[2]:08X}/{row[3]:08X} insn={row[4]:08X} "
            f"raw={row[8]:08X} guest={row[9]:08X} exec={row[10]:08X} "
            f"hicode={row[16]:08X}:{row[17]:08X} aux={row[23]:08X}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
