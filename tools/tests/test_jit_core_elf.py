#!/usr/bin/env python3
"""Execute linked production JIT address, metadata and invalidation routines."""
import argparse
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_CPSR
from test_hicode_dispatch_elf import load_elf, execute

parser = argparse.ArgumentParser()
parser.add_argument("elf", type=Path)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
symbols, sections = load_elf(args.elf)
results = []

def machine():
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for base, size in [(0, 0x8000), (0x02000000, 0x400000), (0x03000000, 0x10000),
                       (0x06800000, 0x100000), (0xFFFF8000, 0x8000), (0x07000000, 0x1000)]:
        uc.mem_map(base, size)
    for address, data in sections:
        uc.mem_write(address, data)
    return uc

def call(uc, name, *values):
    uc.reg_write(UC_ARM_REG_CPSR, 0xD3)
    uc.reg_write(UC_ARM_REG_SP, 0x0300F000)
    uc.reg_write(UC_ARM_REG_LR, 0x07000000)
    for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1), values):
        uc.reg_write(reg, value)
    uc.emu_start(symbols[name], 0x07000000, count=300000)
    assert uc.reg_read(UC_ARM_REG_PC) == 0x07000000, (name, "budget exhausted")
    return uc.reg_read(UC_ARM_REG_R0)

def put(uc, address, value):
    uc.mem_write(address, struct.pack("<I", value))

def passed(name):
    results.append({"case": name, "result": "PASS"})

uc = machine()
cache, table, state = (symbols[name] for name in ("sdc_cache", "sdc_romBlockToCacheBlock", "gJitState"))
for logical, expected in [(0x08000000, 0x02200000), (0x081FFFFC, 0x023FFFFC), (0x03001001, 0x03001001)]:
    assert call(uc, "jit_getBackingAddress", logical) == expected
    passed(f"backing-{logical:08x}")
for address in (0x08200000, 0x09ED4000, 0x0A200000, 0x0C200000):
    index = ((address & 0x01FFFFFF) >> 12) * 4
    put(uc, table + index, cache + 3 * 4096)
    for half in (0, 1, 2, 3):
        assert call(uc, "jit_getBackingAddress", address + half) == cache + 3 * 4096 + half
    put(uc, table + index, 0)
    assert call(uc, "jit_isBlockJitted", address) == 0
    assert call(uc, "jit_getBackingAddress", address) == address
    passed(f"mirror-loaded-missing-{address:08x}")

# Known ARM B/BL encodings plus an independent signed displacement oracle.
vectors = [(0x02200000, 0xEA7B4FFE, 0x09ED4000), (0x023FFFF8, 0xEA000000, 0x08200000),
           (0x02200000, 0xEA000000, 0x02200008), (0x09ED4000, 0xEA0003FE, 0x09ED5000),
           (0x08200000, 0xEAFFFFFE, 0x08200000), (0x08200000, 0xEAFFFFFD, 0x081FFFFC)]
for pc, instruction, target in vectors:
    assert call(uc, "jit_calculateArmBranchTarget", pc + 4, instruction) == target
    passed(f"branch-{pc:08x}-{instruction:08x}")

# Cache metadata layout follows the public jit_state_t arrays, in bytes.
dynamic_bits = 2 * 1024 * 1024 // 16 + 32 * 1024 // 16
aux_base = (2 * 1024 * 1024 + 32 * 1024 + 1024 * 1024 + 256 * 1024 + 96 * 1024) // 16 + 4
dynamic_aux = aux_base + 2 * 1024 * 1024 // 8 + 32 * 1024 // 8
for enabled in (0, 1):
    uc.mem_write(symbols["sJitEnabled"], bytes([enabled]))
    for slot in (0, 1, 255):
        bits = state + dynamic_bits + slot * 256
        aux = state + dynamic_aux + slot * 512
        uc.mem_write(bits, b"\xa5" * 256)
        uc.mem_write(aux, b"\xa5" * 512)
        call(uc, "jit_resetDynamicRomBlock", cache + slot * 4096)
        assert uc.mem_read(bits, 256) == bytes([0 if enabled else 255]) * 256
        assert uc.mem_read(aux, 512) == b"\0" * 512
        passed(f"cache-reset-{enabled}-{slot}")
before = bytes(uc.mem_read(state + dynamic_bits, 65536))
for ptr in (cache - 4096, cache + 1, cache + 1024 * 1024):
    call(uc, "jit_resetDynamicRomBlock", ptr)
assert bytes(uc.mem_read(state + dynamic_bits, 65536)) == before
passed("invalid-cache-reset-preserves-metadata")

uc.mem_write(symbols["sJitEnabled"], b"\1")
address = 0x08200102
index = ((address & 0x01FFFFFF) >> 12) * 4
put(uc, table + index, cache)
bit_offset = call(uc, "jit_getJitBitsOffset", cache + 0x102)
uc.mem_write(state + bit_offset, b"\x02")
assert call(uc, "jit_isBlockJitted", address) == 1
put(uc, table + index, cache + 4096)
call(uc, "jit_resetDynamicRomBlock", cache + 4096)
assert call(uc, "jit_isBlockJitted", address) == 0
passed("cache-replacement-rejects-stale-jit-bit")

# Stub only block compilation; execute the caller's backing resolution and CP15 order.
for guest in (0x08200100, 0x08200101, 0x08000100, 0x08000101):
    uc = machine()
    put(uc, table + 512 * 4, cache)
    events = []
    def hook(cpu, pc, size, _):
        if pc in (symbols["jit_processArmBlock"], symbols["jit_processThumbBlock"]):
            events.append(("thumb" if pc == symbols["jit_processThumbBlock"] else "arm", cpu.reg_read(UC_ARM_REG_R0)))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
            return
        if size != 4 or cpu.reg_read(UC_ARM_REG_CPSR) & 32:
            return
        op, = struct.unpack("<I", cpu.mem_read(pc, 4))
        if op & 0x0F000010 == 0x0E000010 and (op >> 8) & 15 == 15:
            assert not op & (1 << 20), "unexpected CP15 read"
            events.append(("cp15", (op >> 16) & 15, op & 15, (op >> 5) & 7))
            cpu.reg_write(UC_ARM_REG_PC, pc + 4)
    uc.hook_add(UC_HOOK_CODE, hook)
    assert call(uc, "jit_ensureBlockJitted", guest) == guest
    expected_backing = (cache if guest >= 0x08200000 else 0x02200000) + 0x100
    assert events[0] == ("thumb" if guest & 1 else "arm", expected_backing), events
    assert events.index(("cp15", 6, 4, 0)) < events.index(("cp15", 7, 5, 0)), events
    passed(f"patch-backing-and-unmap-before-invalidate-{guest:08x}")

for base in (0x08200000, 0x09FFF000, 0x0A200000, 0x0C200000):
    for offset, spsr in ((0, 0x10), (0, 0x30), (2, 0xA0000030), (0x7FC, 0x10), (0x7FE, 0x30)):
        r = execute(symbols, sections, spsr=spsr, address=base+offset, word=0xB170BC01, mapped=base)
        assert r["path"] == ("thumb" if spsr & 32 else "arm")
        passed(f"mapped-dispatch-edge-{base+offset:08x}-{spsr:08x}")

report = {"tests": results, "scope": "linked production routines; mocked CP15 and block compiler; no physical cache or timing claim"}
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + "\n")
print(f"PASS: {len(results)} linked JIT/address/cache regressions")
