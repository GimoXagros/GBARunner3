#!/usr/bin/env python3
"""Execute the linked production hicode handler with controlled CP15 read results.

Unicorn executes the real ARM machine code, register banks, and DTCM loads.
Only the cache-debug index/data interface is supplied by this fixture; this
does not test physical cache coherence. No game ROM or BIOS is needed.
Dependencies: unicorn==2.1.4, pyelftools==0.32.
"""
import argparse
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_CPSR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR,
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12,
)

REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC]
SENTINEL = 0xEE01E801


def load_elf(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                    if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS' and s['sh_size']]
    return symbols, sections


def execute(symbols, sections, *, spsr, address, word, mapped, force_arm=False):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for base, size in [(0, 0x8000), (0x02000000, 0x400000), (0x03000000, 0x10000),
                       (0x06800000, 0x100000), (0xFFFF8000, 0x8000)]:
        uc.mem_map(base, size)
    for addr, data in sections:
        uc.mem_write(addr, data)
    def put(name, value):
        uc.mem_write(symbols[name], struct.pack('<I', value))
    put('gHicodeState', mapped)
    put('vm_undefinedSpsr', spsr)
    # Initialize the FIQ scratch stack, then simulate vm_undefined's UND entry.
    uc.reg_write(UC_ARM_REG_CPSR, 0xD1)
    uc.reg_write(UC_ARM_REG_SP, 0xFFFFD800)
    uc.reg_write(UC_ARM_REG_CPSR, 0xDB)
    uc.reg_write(UC_ARM_REG_SP, spsr)
    exception_lr = address + (2 if spsr & 32 else 4)
    uc.reg_write(UC_ARM_REG_LR, exception_lr)
    for i in range(8):
        uc.reg_write(REGS[i], 0x11220000 + i)
    index = None
    observed = {}
    endpoints = {symbols['vm_undefinedArmInstructionInLR']: 'arm',
                 symbols['vm_undefinedThumbInstructionInLR']: 'thumb',
                 symbols['hic_mapRomBlock']: 'miss'}
    def hook(cpu, pc, size, _):
        nonlocal index
        if pc in endpoints:
            observed.update(path=endpoints[pc], lr=cpu.reg_read(UC_ARM_REG_LR),
                            r11=cpu.reg_read(UC_ARM_REG_R11), r0=cpu.reg_read(UC_ARM_REG_R0),
                            sp=cpu.reg_read(UC_ARM_REG_SP), cpsr=cpu.reg_read(UC_ARM_REG_CPSR))
            cpu.emu_stop()
            return
        op, = struct.unpack('<I', cpu.mem_read(pc, 4))
        if op & 0x0F000010 == 0x0E000010 and (op >> 8) & 15 == 15:
            rd, crn, crm = (op >> 12) & 15, (op >> 16) & 15, op & 15
            assert crn == 15 and crm in (0, 3), f'unexpected CP15 operation {op:08X}'
            if op & (1 << 20):
                cpu.reg_write(REGS[rd], word if crm == 3 else index)
                # Negative control reproduces the original unconditional ARM dispatch.
                if force_arm and crm == 3 and rd == 14:
                    cpu.reg_write(UC_ARM_REG_PC, symbols['vm_undefinedArmInstructionInLR'])
                    return
            else:
                assert crm == 0
                index = cpu.reg_read(REGS[rd])
            cpu.reg_write(UC_ARM_REG_PC, pc + 4)
    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(symbols['hic_undefinedHicodeMiss'], 0xFFFFFFFF, count=100)
    assert observed, 'handler did not reach a dispatch boundary'
    assert index == address
    if observed['path'] != 'miss':
        assert [uc.reg_read(REGS[i]) for i in range(8)] == [0x11220000 + i for i in range(8)]
    return observed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    symbols, sections = load_elf(args.elf)
    cases = [
        ('mapped-arm', 0x60000010, 0x09FE3010, 0xE1B0009E, 0x09FE3000, 'arm'),
        ('thumb-low', 0x60000030, 0x09FE3010, 0xB170B100, 0x09FE3000, 'thumb'),
        ('thumb-high', 0xA0000030, 0x09FE3012, 0xB170BC01, 0x09FE3000, 'thumb'),
        ('arm-sentinel', 0x10, 0x09FE3010, SENTINEL, 0x09FE3000, 'miss'),
        ('thumb-sentinel', 0x30, 0x09FE3012, SENTINEL, 0x09FE3000, 'miss'),
        ('different-segment', 0x30, 0x09FE3812, 0xB170BC01, 0x09FE3000, 'miss'),
    ]
    results = []
    for name, spsr, address, word, mapped, expected in cases:
        kwargs = dict(spsr=spsr, address=address, word=word, mapped=mapped)
        r = execute(symbols, sections, **kwargs)
        assert r['path'] == expected, (name, r)
        if expected == 'thumb':
            half = (word >> (16 if address & 2 else 0)) & 0xFFFF
            assert r['lr'] & 0xFFFF == half and r['r11'] == address, (name, r)
            negative = execute(symbols, sections, **kwargs, force_arm=True)
            assert negative['path'] == 'arm', 'negative control did not reintroduce ARM misdispatch'
        if expected == 'arm':
            assert r['lr'] == word and r['r11'] == address + 4
        if expected == 'miss':
            assert r['r0'] == address
            assert r['sp'] == symbols['dtcmHicodeStackEnd'] - 24
        results.append({'case': name, 'result': 'PASS', 'boundary': r})
    report = {'elf': str(args.elf), 'tests': results,
              'negative_controls': 'both Thumb cases reject original ARM-only dispatch',
              'scope': 'linked ARM handler; controlled CP15 cache-debug results; no physical cache claim'}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
