#!/usr/bin/env python3
"""Execute the ARM7 RTC clock IPC code with synthetic RTC/timer MMIO.

The linked IRQ mask instructions, timer setup and snapshot arithmetic execute;
physical oscillator accuracy, interrupt latency and console sleep are not tested.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR
from test_hicode_dispatch_elf import load_elf, REGS
from test_rtc_loader_elf import find_symbol

TIMER = 0x04000108
IRQ_FLAGS = 0x04000214
TIMER_IRQ = 1 << 5
SAMPLE, OBJECT, STACK, RETURN = 0x02010020, 0x02011000, 0x02020000, 0x02012000
DATE = bytes((0x24, 0x02, 0x29, 3, 0xA0, 0x45, 0x50))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    symbols, sections = load_elf(args.elf)
    entry = find_symbol(symbols, "RtcIpcService13HandleMessage")
    start = find_symbol(symbols, "RtcIpcService5Start")
    irq = find_symbol(symbols, "rtcTimerIrq")
    high_address = find_symbol(symbols, "sRtcTimerOverflows")
    thread_start = symbols["_ZN16ThreadIpcService5StartEv"]
    results = []

    def run(address, high=0, lows=(0,), pending=False, cpsr=0x1F):
        cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for base, size in ((0x02000000, 0x1000000), (0x037C0000, 0x50000),
                           (0x04000000, 0x2000)):
            cpu.mem_map(base, size)
        for base, data in sections:
            cpu.mem_write(base, data)
        cpu.mem_write(high_address, struct.pack("<I", high))
        cpu.mem_write(SAMPLE - 8, b"\xA5" * 32)
        cpu.mem_write(OBJECT, struct.pack("<II", 0, 18))
        cpu.mem_write(0x04000210, struct.pack("<I", 0x17))
        for timer in (0, 1, 3):
            cpu.mem_write(0x04000100 + timer * 4, struct.pack("<I", 0x12340000 + timer))
        cpu.reg_write(UC_ARM_REG_CPSR, cpsr)
        cpu.reg_write(UC_ARM_REG_SP, STACK)
        cpu.reg_write(UC_ARM_REG_LR, RETURN)
        for i in range(4, 12):
            cpu.reg_write(REGS[i], 0xA5000000 + i)
        cpu.reg_write(REGS[0], OBJECT if address != irq else TIMER_IRQ)
        cpu.reg_write(REGS[1], SAMPLE >> 2)
        reads, replies, handlers = [], [], []

        def ret():
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        def hook(machine, pc, size, _):
            if pc == symbols["rtc_readRegister"]:
                assert machine.reg_read(REGS[0]) == 0x26
                assert machine.reg_read(REGS[1]) == SAMPLE
                assert machine.reg_read(REGS[2]) == 7
                machine.mem_write(SAMPLE, DATE)
                ret()
            elif pc == symbols["ipc_sendFifoMessage"]:
                replies.append((machine.reg_read(REGS[0]), machine.reg_read(REGS[1])))
                ret()
            elif pc == symbols["rtos_setIrqFunc"]:
                handlers.append((machine.reg_read(REGS[0]), machine.reg_read(REGS[1])))
                ret()
            elif pc in (symbols["rtc_init"], thread_start):
                ret()

        def memory(machine, access, at, size, value, _):
            if at == TIMER:
                assert machine.reg_read(UC_ARM_REG_CPSR) & 0x80
                low = lows[min(len(reads), len(lows) - 1)]
                reads.append(low)
                machine.mem_write(TIMER, struct.pack("<H", low))
            elif at == IRQ_FLAGS:
                assert machine.reg_read(UC_ARM_REG_CPSR) & 0x80
                machine.mem_write(IRQ_FLAGS, struct.pack("<I", TIMER_IRQ if pending else 0))

        cpu.hook_add(UC_HOOK_CODE, hook)
        cpu.hook_add(UC_HOOK_MEM_READ, memory)
        cpu.emu_start(address, RETURN, count=1000)
        assert cpu.reg_read(UC_ARM_REG_PC) == RETURN
        assert cpu.reg_read(UC_ARM_REG_SP) == STACK
        assert cpu.reg_read(UC_ARM_REG_CPSR) & 0x9F == cpsr & 0x9F
        assert [cpu.reg_read(REGS[i]) for i in range(4, 12)] == [0xA5000000 + i for i in range(4, 12)]
        assert bytes(cpu.mem_read(SAMPLE - 8, 8)) == b"\xA5" * 8
        assert bytes(cpu.mem_read(SAMPLE + 16, 8)) == b"\xA5" * 8
        for timer in (0, 1, 3):
            assert struct.unpack("<I", cpu.mem_read(0x04000100 + timer * 4, 4))[0] == 0x12340000 + timer
        return cpu, reads, replies, handlers

    cpu, _, _, handlers = run(start, high=7)
    assert struct.unpack("<HH", cpu.mem_read(TIMER, 4)) == (0, 0xC3)
    assert struct.unpack("<I", cpu.mem_read(high_address, 4))[0] == 0
    assert struct.unpack("<I", cpu.mem_read(0x04000210, 4))[0] == 0x17 | TIMER_IRQ
    assert handlers == [(TIMER_IRQ, irq)]
    results.append("Start reserves ARM7 timer2 at /1024 and registers overflow handler")

    cpu, _, _, _ = run(irq, high=0x12345678)
    assert struct.unpack("<I", cpu.mem_read(high_address, 4))[0] == 0x12345679
    results.append("linked overflow handler increments high counter")

    for cpsr in (0x1F, 0x9F):
        for name, high, lows, pending, expected in (
            ("ordinary", 0x12345678, (0xABCD,), False, 0x12345678ABCD),
            ("before-wrap", 7, (0xFFFF,), False, 0x7FFFF),
            ("pending-wrap", 7, (4, 4), True, 0x80004),
            ("wrap-between-low-and-flags", 7, (0xFFFF, 1), True, 0x80001),
        ):
            cpu, reads, replies, _ = run(entry, high, lows, pending, cpsr)
            sample = bytes(cpu.mem_read(SAMPLE, 16))
            assert sample[:8] == DATE + b"\0"
            assert struct.unpack_from("<Q", sample, 8)[0] == expected, name
            assert len(reads) == (2 if pending else 1), (name, reads)
            assert replies == [(18, 1)]
            results.append(f"{name}: ticks and preserved IRQ state {cpsr:02X}")

    # Suppress the pending flag at the MMIO boundary: the same linked code must
    # expose the stale high word, demonstrating sensitivity to the wrap guard.
    cpu, _, _, _ = run(entry, 7, (0xFFFF, 1), False)
    assert struct.unpack("<Q", cpu.mem_read(SAMPLE + 8, 8))[0] != 0x80001
    results.append("negative control: lost overflow indication changes observed ticks")
    report = {"elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
              "results": results, "hardware_timing_tested": False}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
