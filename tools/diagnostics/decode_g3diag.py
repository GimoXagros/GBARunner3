#!/usr/bin/env python3
"""Decode persistent GBARunner3 runtime-state diagnostic checkpoints."""

from __future__ import annotations

import argparse
import csv
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x47443347
HEADER = struct.Struct("<16I")
PREFIX_V2 = struct.Struct("<17I15H4H1H4I3I")
PREFIX_V3 = struct.Struct("<17I21H4H1H4I3I4I")
DMA = struct.Struct("<IIHHII")

CORE_COLUMNS = [
    "sample", "irq_pc", "emulated_pc", "cpsr", "irq_state", "hw_irq_mask",
    "forced_irq_mask", "hicode_block", "hicode_block_mask", "sram_reads",
    "sram_writes", "last_sram_address", "last_sram_value", "dma_starts",
    "last_dma_channel", "dma_flags", "sd_forbidden_range",
]
DISPLAY_V2_COLUMNS = [
    "dispcnt", "dispstat", "vcount", "bg0cnt", "bg1cnt", "bg2cnt", "bg3cnt",
    "winin", "winout", "mosaic", "bldcnt", "bldalpha", "bldy", "bg2pa", "bg2pd",
]
DISPLAY_V3_COLUMNS = DISPLAY_V2_COLUMNS[:-2] + [
    "bg2pa", "bg2pb", "bg2pc", "bg2pd", "bg2x_l", "bg2x_h", "bg2y_l", "bg2y_h",
]
IRQ_COLUMNS = ["ie", "if", "waitcnt", "ime"]
TAIL_COLUMNS = [
    "nds_keyinput", "timer0", "timer1", "timer2", "timer3",
    "sound0", "sound1", "sound2",
]
HARDWARE_COLUMNS = ["ds_dispcnt", "ds_dispstat_vcount", "ds_ie", "ds_if"]


@dataclass(frozen=True)
class Checkpoint:
    path: Path
    header: tuple[int, ...]
    rows: list[tuple[int, ...]]
    columns: list[str]

    @property
    def checkpoint_sequence(self) -> int:
        return self.header[9] if self.header[1] >= 3 else 0


def fnv1a(data: bytes, value: int = 2166136261) -> int:
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def dma_columns() -> list[str]:
    result = []
    for channel in range(4):
        result.extend(
            f"dma{channel}_{field}"
            for field in (
                "source", "destination", "count", "control",
                "current_source", "current_destination",
            )
        )
    return result


def read_checkpoint(path: Path) -> Checkpoint:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"{path}: shorter than the diagnostic header")
    header = HEADER.unpack_from(data)
    magic, version, header_size, record_size, capacity, write_index, total = header[:7]
    if magic != MAGIC:
        raise ValueError(f"{path}: not a G3DG diagnostic checkpoint")
    if version not in (1, 2, 3) or header_size != HEADER.size:
        raise ValueError(f"{path}: unsupported diagnostic format version {version}")
    if capacity == 0 or capacity & (capacity - 1) or write_index >= capacity:
        raise ValueError(f"{path}: invalid ring geometry")

    prefix = PREFIX_V3 if version == 3 else PREFIX_V2
    expected_record_size = prefix.size + 4 * DMA.size
    if record_size != expected_record_size:
        raise ValueError(
            f"{path}: record size {record_size} does not match version {version}"
        )

    if total == 0:
        if len(data) != HEADER.size:
            raise ValueError(f"{path}: ready marker has trailing data")
        columns = CORE_COLUMNS + (DISPLAY_V3_COLUMNS if version == 3 else DISPLAY_V2_COLUMNS)
        columns += IRQ_COLUMNS + TAIL_COLUMNS
        if version == 3:
            columns += HARDWARE_COLUMNS
        columns += dma_columns()
        return Checkpoint(path, header, [], columns)

    expected_size = header_size + capacity * record_size
    if len(data) != expected_size:
        raise ValueError(f"{path}: incomplete checkpoint ({len(data)} != {expected_size})")
    if version == 3:
        checksum_header = bytearray(data[:header_size])
        struct.pack_into("<I", checksum_header, 13 * 4, 0)
        checksum = fnv1a(data[header_size:], fnv1a(checksum_header))
        if checksum != header[13]:
            raise ValueError(
                f"{path}: checksum mismatch (0x{checksum:08X} != 0x{header[13]:08X})"
            )

    display_count = 21 if version == 3 else 15
    present = min(total, capacity)
    oldest = write_index if total >= capacity else 0
    rows = []
    for logical in range(present):
        physical = (oldest + logical) % capacity
        offset = header_size + physical * record_size
        values = prefix.unpack_from(data, offset)
        core = values[:17]
        display_end = 17 + display_count
        display = values[17:display_end]
        irq = values[display_end:display_end + 4]
        key = values[display_end + 4]
        timers = values[display_end + 5:display_end + 9]
        sound = values[display_end + 9:display_end + 12]
        hardware = values[display_end + 12:display_end + 16] if version == 3 else ()
        dma_values = []
        for channel in range(4):
            dma_values.extend(DMA.unpack_from(data, offset + prefix.size + channel * DMA.size))
        rows.append(core + display + irq + (key,) + timers + sound + hardware + tuple(dma_values))

    columns = CORE_COLUMNS + (DISPLAY_V3_COLUMNS if version == 3 else DISPLAY_V2_COLUMNS)
    columns += IRQ_COLUMNS + TAIL_COLUMNS
    if version == 3:
        columns += HARDWARE_COLUMNS
    columns += dma_columns()
    return Checkpoint(path, header, rows, columns)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("dumps", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    return parser.parse_args()


def hex_value(value: int) -> str:
    return f"0x{value:08X}"


def summarize(checkpoint: Checkpoint) -> None:
    if not checkpoint.rows:
        return
    index = {name: position for position, name in enumerate(checkpoint.columns)}
    rows = checkpoint.rows
    pcs = {row[index["emulated_pc"]] for row in rows}
    modes = {row[index["dispcnt"]] & 7 for row in rows}
    vcounts = {row[index["vcount"]] for row in rows}
    dma_delta = rows[-1][index["dma_starts"]] - rows[0][index["dma_starts"]]
    timer_changes = []
    for timer in range(4):
        name = f"timer{timer}"
        values = {row[index[name]] & 0xFFFF for row in rows}
        if len(values) > 1:
            timer_changes.append(str(timer))
    print(
        f"runtime summary: unique_pc={len(pcs)} modes={sorted(modes)} "
        f"vcount_values={len(vcounts)} dma_start_delta={dma_delta} "
        f"changing_timers={','.join(timer_changes) or 'none'}"
    )


def main() -> int:
    args = parse_args()
    checkpoints = []
    for path in args.dumps:
        try:
            checkpoint = read_checkpoint(path)
        except ValueError as error:
            print(error)
            continue
        version = checkpoint.header[1]
        status_names = {
            1: "ready" if version == 3 else "armed",
            2: "armed" if version == 3 else "triggered",
            3: "checkpoint" if version == 3 else "captured",
            4: "write-failed",
        }
        status = status_names.get(checkpoint.header[11], f"unknown-{checkpoint.header[11]}")
        integrity = "checksum=ok" if version == 3 and checkpoint.rows else "legacy/no-ring"
        print(
            f"{path}: version={version} sequence={checkpoint.checkpoint_sequence} "
            f"status={status} samples={len(checkpoint.rows)} {integrity}"
        )
        checkpoints.append(checkpoint)

    if not checkpoints:
        raise SystemExit("no valid G3DG checkpoint")
    checkpoint = max(checkpoints, key=lambda item: item.checkpoint_sequence)
    output = args.output or checkpoint.path.with_suffix(".csv")
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(checkpoint.columns)
        for row in checkpoint.rows:
            writer.writerow(hex_value(value) for value in row)

    game_code = checkpoint.header[7].to_bytes(4, "little").decode("ascii", errors="replace")
    flags = checkpoint.header[10]
    enabled = [
        name
        for bit, name in enumerate(
            ("no-bg-vram-abort", "no-vram-write-buffer", "no-jit", "safe-dma")
        )
        if flags & (1 << bit)
    ]
    variant = ",".join(enabled) if enabled else "baseline"
    print(
        f"selected {checkpoint.path} sequence={checkpoint.checkpoint_sequence} "
        f"for {game_code} ({variant}); wrote {len(checkpoint.rows)} samples to {output}"
    )
    summarize(checkpoint)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
