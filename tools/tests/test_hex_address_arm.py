#!/usr/bin/env python3
"""Execute the pinned-toolchain parser machine code without a ROM or BIOS."""
import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR

with open(sys.argv[1], "rb") as stream:
    elf = ELFFile(stream)
    segments = [(s["p_vaddr"], s.data()) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    entry = elf.get_section_by_name(".symtab").get_symbol_by_name("hex_parse")[0]["st_value"]

cases = [(b"0", 0), (b"1", 1), (b"FFFFFFFF", 0xFFFFFFFF), (b"0x08000000", 0x08000000),
         (b"0X08000000", 0x08000000), (b"aBcDeF", 0xABCDEF)]
cases += [(v, None) for v in (None, b"", b"0x", b"0X", b"123456789", b"0x100000000", b"-1", b"+1",
                              b" 1", b"1 ", b"1g", b"g", b"1\0bad")]
for value, expected in cases:
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02000000, 0x20000)
    for address, data in segments:
        uc.mem_write(address, data)
    ptr, out, stop = 0x02010000, 0x02011000, 0x02012000
    if value is not None:
        uc.mem_write(ptr, value + b"\0")
    uc.mem_write(out, struct.pack("<I", 0x12345678))
    uc.reg_write(UC_ARM_REG_R0, ptr if value is not None else 0)
    uc.reg_write(UC_ARM_REG_R1, len(value) if value is not None else 0)
    uc.reg_write(UC_ARM_REG_R2, out)
    uc.reg_write(UC_ARM_REG_SP, 0x0201F000)
    uc.reg_write(UC_ARM_REG_LR, stop)
    uc.emu_start(entry, stop, count=10000)
    assert uc.reg_read(UC_ARM_REG_R0) == (expected is not None), value
    assert struct.unpack("<I", uc.mem_read(out, 4))[0] == (expected if expected is not None else 0x12345678), value
print(f"PASS: {len(cases)} linked ARM parser cases")
