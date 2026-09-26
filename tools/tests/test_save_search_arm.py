#!/usr/bin/env python3
"""Execute production search helper and original assembly with cache eviction."""
from pathlib import Path
import re
import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[2]
with open(sys.argv[1], "rb") as stream:
    elf = ELFFile(stream)
    segments = [(s["p_vaddr"], s.data()) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}

corpus = []
for name in ("SaveEeprom.cpp", "SaveFlash.cpp", "SaveSram.cpp"):
    source = (ROOT / "code/core/arm9/source/Save" / name).read_text()
    for values in re.findall(r"const u32\s+\w+\[(?:4)?\]\s*=\s*\{([^}]+)\}", source):
        words = [int(v.rstrip("uU"), 16) for v in values.replace(" ", "").split(",")]
        assert len(words) == 4
        corpus.append(struct.pack("<4I", *words))

def run(signature, rom, start, end, expected, reject=None):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02000000, 0x40000)
    for address, data in segments:
        uc.mem_write(address, data)
    ptr, slot, stop = 0x02020000, 0x02022000, 0x02021000
    loaded = [0, 0]
    def hook(machine, pc, size, _):
        if pc == symbols["probe_get_block"]:
            offset = machine.reg_read(UC_ARM_REG_R0) - 0x08000000
            assert 0 <= offset < len(rom) and offset % 4096 == 0
            if offset == reject:
                machine.reg_write(UC_ARM_REG_R0, 0)
            else:
                block = bytes(rom[offset:offset + 4096])
                machine.mem_write(slot, block + b"\xcd" * (4096 - len(block)))
                loaded[:] = [offset, len(block)]
                machine.reg_write(UC_ARM_REG_R0, slot)
            machine.reg_write(UC_ARM_REG_PC, machine.reg_read(UC_ARM_REG_LR))
    def read(machine, access, address, size, value, _):
        if slot <= address < slot + 4096:
            logical = loaded[0] + address - slot
            assert address + size <= slot + loaded[1], ("ROM overread", logical, size)
            assert start <= logical and logical + size <= end, ("range overread", logical, size)
    uc.hook_add(UC_HOOK_CODE, hook, begin=symbols["probe_get_block"], end=symbols["probe_get_block"])
    uc.hook_add(UC_HOOK_MEM_READ, read)
    uc.mem_write(ptr, signature)
    for reg, value in ((UC_ARM_REG_R0, ptr), (UC_ARM_REG_R1, 0x08000000 + start),
                       (UC_ARM_REG_R2, 0x08000000 + end), (UC_ARM_REG_SP, 0x0203F000), (UC_ARM_REG_LR, stop)):
        uc.reg_write(reg, value)
    uc.emu_start(symbols["search_probe"], stop, count=200000)
    assert uc.reg_read(UC_ARM_REG_PC) == stop, "instruction budget exhausted"
    result = uc.reg_read(UC_ARM_REG_R0)
    assert result == (0xFFFFFFFF if expected is None else 0x08000000 + expected), (start, end, expected, hex(result))


def run_fast(signature, data, expected):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02000000, 0x40000)
    for address, segment in segments:
        uc.mem_write(address, segment)
    ptr, slot, stop = 0x02020000, 0x02022000, 0x02021000
    uc.mem_write(ptr, signature)
    uc.mem_write(slot, bytes(data))
    for reg, value in ((UC_ARM_REG_R0, slot), (UC_ARM_REG_R1, len(data)),
                       (UC_ARM_REG_R2, ptr), (UC_ARM_REG_SP, 0x0203F000), (UC_ARM_REG_LR, stop)):
        uc.reg_write(reg, value)
    uc.emu_start(symbols["fast_probe"], stop, count=200000)
    assert uc.reg_read(UC_ARM_REG_PC) == stop, "fast-search instruction budget exhausted"
    result = uc.reg_read(UC_ARM_REG_R0)
    assert result == (0 if expected is None else slot + expected), (expected, hex(result))


def run_seam(signature, tail, head, head_length, expected):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02000000, 0x40000)
    for address, segment in segments:
        uc.mem_write(address, segment)
    ptr, tail_ptr, head_ptr, stop = 0x02020000, 0x02021000, 0x02021020, 0x02022000
    uc.mem_write(ptr, signature)
    uc.mem_write(tail_ptr, bytes(tail))
    uc.mem_write(head_ptr, bytes(head))
    for reg, value in ((UC_ARM_REG_R0, ptr), (UC_ARM_REG_R1, tail_ptr),
                       (UC_ARM_REG_R2, head_ptr), (UC_ARM_REG_R3, head_length),
                       (UC_ARM_REG_SP, 0x0203F000), (UC_ARM_REG_LR, stop)):
        uc.reg_write(reg, value)
    uc.emu_start(symbols["seam_probe"], stop, count=200000)
    assert uc.reg_read(UC_ARM_REG_PC) == stop, "seam-search instruction budget exhausted"
    assert uc.reg_read(UC_ARM_REG_R0) == expected, (head_length, expected, hex(uc.reg_read(UC_ARM_REG_R0)))

count = 0
for signature in corpus:
    for pos in list(range(4081, 4098)) + [0, 100, 4080, 8000]:
        rom = bytearray(b"\xa5" * 8192)
        rom[pos:pos + 16] = signature
        run(signature, rom, 0, len(rom), pos if pos % 4 == 0 else None)
        count += 1
    rom = bytearray(b"\xa5" * 4108)
    rom[4092:4108] = signature
    for start, end, expected in [(0, 4108, 4092), (0, 4107, None), (4092, 4108, 4092), (4093, 4108, None)]:
        run(signature, rom, start, end, expected)
        count += 1
    run(signature, rom, 0, 4108, None, reject=4096)
    count += 1
    run(signature, rom, 0, 4108, None, reject=0)
    count += 1
print(f"PASS: {count} linked ARM search cases, {len(corpus)} source signatures, one-slot eviction, ROM/range read bounds")

for signature in corpus:
    for length in (64,4096):
        rom=bytearray(b'\xa5'*length)
        rom[length-32:length-28]=signature[:4]
        rom[length-24:length-16]=signature[:8]
        rom[length-16:length]=signature
        run(signature,rom,0,length,length-16)
        count+=1
print(f'PASS: {count} linked cases including false-prefix last-window advancement')

for signature in corpus:
    for false_prefix in (0, 4, 28, 128, 2048):
        rom = bytearray(b'\xa5' * 4096)
        rom[false_prefix:false_prefix + 4] = signature[:4]
        rom[false_prefix + 4:false_prefix + 20] = signature
        run(signature, rom, 0, len(rom), false_prefix + 4)
        count += 1
        run_fast(signature, rom, false_prefix + 4)
        count += 1
    for length in (64, 4096):
        rom = bytearray(b'\xa5' * length)
        rom[length - 32:length - 28] = signature[:4]
        rom[length - 24:length - 16] = signature[:8]
        rom[length - 16:length] = signature
        run_fast(signature, rom, length - 16)
        count += 1
print(f'PASS: {count} linked helper/direct-scanner cases including interior false-prefix recovery')

for signature in corpus:
    for offset in (0, 4, 8):
        tail = bytearray(b'\xa5' * 12)
        head = bytearray(b'\xa5' * 12)
        tail_bytes = 12 - offset
        tail[offset:] = signature[:tail_bytes]
        head[:16 - tail_bytes] = signature[tail_bytes:]
        run_seam(signature, tail, head, 16 - tail_bytes, 0x08200000 - 12 + offset)
        count += 1
        run_seam(signature, tail, head, 15 - tail_bytes, 0xffffffff)
        count += 1
print(f'PASS: {count} linked cases including linear/high-ROM seam and truncated suffix')
