#!/usr/bin/env python3
"""Check linked RTC boot interworking with the ARM946E-S L4 bit enabled.

Unicorn's default ARMv5 PC-load behavior switches to Thumb on an odd target.
The application's crt0 enables ARMv4T compatibility, where ARM LDR/LDM PC
does not switch state. This probe models that specific mismatch at linked
veneers and checks actual FatFs return opcodes plus observed link-registers.
It does not emulate the full boot, DLDI device, or CP15 behavior generally.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_LR

from test_hicode_dispatch_elf import load_elf
from test_rtc_loader_elf import HEAP_BASE, LinkedRtc, STATUS_FRESH, find_symbol


class L4AwareRtc(LinkedRtc):
    def __init__(self, symbols, sections):
        self.unsafe_pc_load: tuple[int, int] | None = None
        self.fatfs_entries: list[tuple[str, int]] = []
        super().__init__(symbols, sections)

    def _hook(self, cpu, pc, size, user):
        if not cpu.reg_read(UC_ARM_REG_CPSR) & 0x20:  # ARM state
            word = struct.unpack("<I", cpu.mem_read(pc, 4))[0]
            if word == 0xE51FF004:  # ARM LDR pc, [pc, #-4] veneer
                target = struct.unpack("<I", cpu.mem_read(pc + 4, 4))[0]
                if target & 1:
                    self.unsafe_pc_load = (pc, target)
                    cpu.emu_stop()
                    return
        for name in ("f_open", "f_stat"):
            if name in self.symbols and pc == self.symbols[name]:
                self.fatfs_entries.append((name, cpu.reg_read(UC_ARM_REG_LR)))
        super()._hook(cpu, pc, size, user)


def has_arm_pop_pc(elf: Path, cpu: L4AwareRtc, name: str) -> bool:
    with elf.open("rb") as stream:
        symbols = ELFFile(stream).get_section_by_name(".symtab")
        matches = [symbol for symbol in symbols.iter_symbols() if symbol.name == name]
        assert len(matches) == 1, f"expected one {name} symbol"
        size = matches[0]["st_size"]
    address = cpu.symbols[name]
    assert address & 1 == 0, f"{name} must be inspected as ARM code"
    return any(
        (struct.unpack("<I", cpu.cpu.mem_read(address + offset, 4))[0]
         & 0xFFFF8000) == 0xE8BD8000  # ARM LDMIA sp!, {...,pc}
        for offset in range(0, size, 4)
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("arm9", type=Path, help="linked application ARM9 ELF")
    parser.add_argument("--expect-unsafe", action="store_true",
                        help="assert the old binary fails at both named L4 edges")
    args = parser.parse_args()
    symbols, sections = load_elf(args.arm9)
    boot = L4AwareRtc(symbols, sections)
    start = symbols["_start"]
    assert bytes(boot.cpu.mem_read(start, 512)).find(
        struct.pack("<I", 0x0005D07D)) >= 0, \
        "crt0 no longer contains the L4-enabled CP15 control value"

    wrapper = find_symbol(symbols, "RomGpio12LoadRtcState")
    assert wrapper & 1 == 0, "boot RTC caller must start in ARM state"
    try:
        status = boot.call(wrapper, symbols["gRomGpio"], initialize=True)
    except AssertionError:
        if boot.unsafe_pc_load is None:
            raise
        status = None

    direct = L4AwareRtc(symbols, sections)
    assert direct.initialize(HEAP_BASE + 0x800) == STATUS_FRESH, \
        "direct RTC loader seam must reach synthetic missing-media result"
    pop_returns = {name: has_arm_pop_pc(args.arm9, direct, name)
                   for name in ("f_open", "f_stat") if name in symbols}
    unsafe_fatfs = [(name, lr) for name, lr in direct.fatfs_entries
                    if lr & 1 and pop_returns.get(name)]

    if args.expect_unsafe:
        assert boot.unsafe_pc_load is not None, \
            "old binary no longer reproduces ARM PC-load to Thumb RTC entry"
        assert unsafe_fatfs, \
            "old binary no longer reproduces ARM FatFs POP pc to Thumb caller"
        pc, target = boot.unsafe_pc_load
        print(f"FAIL L4 RTC boot veneer at {pc:#x} targets Thumb {target:#x}")
        print("FAIL L4 FatFs return reaches Thumb RTC caller through ARM POP pc")
        print("PASS baseline rejected at both linked L4 boundaries")
        return

    assert boot.unsafe_pc_load is None, \
        "L4 RTC boot veneer loads an odd Thumb target without BX"
    assert not unsafe_fatfs, \
        "L4 FatFs ARM POP pc would return to a Thumb RTC caller"
    assert status == STATUS_FRESH and not boot.media and boot.writes == 0, \
        "linked RTC boot caller did not complete fresh startup without writes"
    print("linked ARM9 RTC boot L4 interworking PASS; synthetic FatFs, "
          "specific PC-load semantics modeled; physical 3DS/DLDI NOT RUN")


if __name__ == "__main__":
    main()
