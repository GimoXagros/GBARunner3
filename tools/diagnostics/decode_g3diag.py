#!/usr/bin/env python3
"""Decode a GBARunner3 .g3diag ring-buffer dump to CSV.

The dump contains emulator state only. It never contains ROM, BIOS, VRAM, or
save-file payloads.
"""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path


MAGIC = 0x47443347
HEADER = struct.Struct("<16I")
PREFIX = struct.Struct("<17I15H4H1H4I3I")
DMA = struct.Struct("<IIHHII")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    data = args.dump.read_bytes()
    if len(data) < HEADER.size:
        raise SystemExit("dump is shorter than the diagnostic header")

    header = HEADER.unpack_from(data)
    magic, version, header_size, record_size, capacity, write_index, total = header[:7]
    if magic != MAGIC:
        raise SystemExit("not a GBARunner3 diagnostic dump")
    if version not in (1, 2) or header_size != HEADER.size:
        raise SystemExit(f"unsupported diagnostic format version {version}")
    if record_size < PREFIX.size + 4 * DMA.size:
        raise SystemExit("diagnostic record size is inconsistent")

    present = min(total, capacity)
    required_size = header_size + (capacity * record_size if present else 0)
    if len(data) < required_size:
        raise SystemExit(
            "diagnostic capture is incomplete; the armed header exists but the ring was not written"
        )
    oldest = write_index if total >= capacity else 0
    rows = []
    for logical in range(present):
        physical = (oldest + logical) % capacity
        offset = header_size + physical * record_size
        if offset + record_size > len(data):
            raise SystemExit("dump ends inside the record ring")
        values = PREFIX.unpack_from(data, offset)
        core = values[:17]
        display = values[17:32]
        irq = values[32:36]
        key_input = values[36]
        timers = values[37:41]
        sound = values[41:44]
        dma_values = []
        dma_offset = offset + PREFIX.size
        for channel in range(4):
            dma_values.extend(DMA.unpack_from(data, dma_offset + channel * DMA.size))
        rows.append(core + display + irq + (key_input,) + timers + sound + tuple(dma_values))

    columns = [
        "sample", "irq_pc", "emulated_pc", "cpsr", "irq_state", "hw_irq_mask", "forced_irq_mask",
        "hicode_block", "hicode_block_mask", "sram_read_seen", "sram_write_seen",
        "last_sram_address", "last_sram_value", "dma_starts", "last_dma_channel",
        "dma_flags", "sd_forbidden_range",
        "dispcnt", "dispstat", "vcount", "bg0cnt", "bg1cnt", "bg2cnt", "bg3cnt",
        "winin", "winout", "mosaic", "bldcnt", "bldalpha", "bldy", "bg2pa", "bg2pd",
        "ie", "if", "waitcnt", "ime",
        "nds_keyinput",
        "timer0", "timer1", "timer2", "timer3",
        "sound0", "sound1", "sound2",
    ]
    for channel in range(4):
        columns.extend(
            f"dma{channel}_{field}"
            for field in ("source", "destination", "count", "control", "current_source", "current_destination")
        )

    output = args.output or args.dump.with_suffix(".csv")
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        for row in rows:
            writer.writerow(f"0x{value:08X}" for value in row)

    game_code = header[7].to_bytes(4, "little").decode("ascii", errors="replace")
    flags = header[10]
    enabled = [
        name
        for bit, name in enumerate(("no-bg-vram-abort", "no-vram-write-buffer", "no-jit", "safe-dma"))
        if flags & (1 << bit)
    ]
    variant = ",".join(enabled) if enabled else "baseline"
    if version >= 2:
        status_names = {1: "armed", 2: "triggered", 3: "captured", 4: "write-failed"}
        status = status_names.get(header[11], f"unknown-{header[11]}")
        print(
            f"status={status}, file_result={header[12]}, dump_attempts={header[13]}, "
            f"trigger_sample={header[14]}"
        )
    print(f"decoded {len(rows)} records for {game_code} ({variant}) to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
