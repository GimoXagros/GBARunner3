#!/usr/bin/env python3
"""Run linked IRQ call sites. Callback bodies/MMIO timing are NOT emulated.

Checks actual linked SP, caller register preservation, stack boundary canary,
capture C/D alternation and hardware dispatch order. Does not prove hardware
latency, callback body stack high-water use, or absence of flicker.
"""
import argparse
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_SPSR, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from test_hicode_dispatch_elf import load_elf, REGS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    symbols, sections = load_elf(args.elf)
    results = []
    callbacks = {symbols[n]: n for n in [*(f"dma_dma{i}Transfer" for i in range(4)),
                                        "dma_dmaSound", "sav_writePendingFiles"]}

    def machine():
        uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for address, size in [(0, 0x8000), (0x02000000, 0x400000), (0x04000000, 0x2000),
                              (0x06800000, 0x100000), (0xFFFF8000, 0x8000)]:
            uc.mem_map(address, size)
        for address, data in sections:
            uc.mem_write(address, data)
        uc.reg_write(UC_ARM_REG_CPSR, 0xD2)
        uc.reg_write(UC_ARM_REG_SPSR, 0x6000001F)
        for i in range(13):
            uc.reg_write(REGS[i], 0x11220000 + i)
        uc.reg_write(UC_ARM_REG_SP, 0x02018000)
        uc.reg_write(UC_ARM_REG_LR, 0x02010004)
        uc.mem_write(0x04000006, struct.pack("<H", 32))
        return uc

    def run(entry, endpoint, expected, mutation=False, uc=None):
        uc = uc or machine()
        before = [uc.reg_read(r) for r in REGS[:13]]
        observed = []
        if mutation:
            # Deliberately add LR to both stack masks at this real linked site.
            start = symbols[entry]
            code = bytes(uc.mem_read(start, 120))
            for offset in range(0, len(code), 4):
                word, = struct.unpack_from("<I", code, offset)
                if word in (0xE92D100F, 0xE8BD100F):
                    uc.mem_write(start + offset, struct.pack("<I", word | 0x4000))
        reached = False

        def hook(cpu, pc, size, _):
            nonlocal reached
            if pc == symbols[endpoint]:
                reached = True
                cpu.emu_stop()
            elif pc in callbacks:
                sp = cpu.reg_read(UC_ARM_REG_SP)
                assert sp % 8 == 0, f"misaligned C callback: {callbacks[pc]} SP={sp:08x}"
                assert symbols["dtcmIrqStack"] + 4 <= sp < symbols["dtcmIrqStackEnd"]
                observed.append(callbacks[pc])
                # An ABI-conforming callback may clobber every caller-saved reg.
                for i in (0, 1, 2, 3, 12):
                    cpu.reg_write(REGS[i], 0xA5A50000 + i)
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        handle = uc.hook_add(UC_HOOK_CODE, hook)
        try:
            uc.emu_start(symbols[entry], 0x02010000, count=300)
            assert reached, (entry, "did not reach IRQ return boundary")
            assert observed == expected, (entry, observed, expected)
            assert [uc.reg_read(r) for r in REGS[:13]] == before, entry
            assert bytes(uc.mem_read(symbols["dtcmIrqStack"], 4)) == struct.pack("<I", 0xDEAD57AC)
        finally:
            uc.hook_del(handle)
        return uc

    channels = [f"dma_dma{i}Transfer" for i in range(4)]
    for entry, endpoint, expected in [
        ("emu_hblankIrq", "emu_hblankIrqReturn", channels),
        ("emu_arm7Irq", "emu_arm7IrqReturn", ["dma_dmaSound"] * 2),
        ("vblankDma", "emu_vblankIrqReturn", channels),
        ("writePendingFiles", "emu_vblankIrqReturn", ["sav_writePendingFiles"]),
    ]:
        run(entry, endpoint, expected)
        results.append(f"{entry}: {len(expected)} linked calls aligned, registers and canary preserved")
    try:
        run("emu_arm7Irq", "emu_arm7IrqReturn", ["dma_dmaSound"] * 2, mutation=True)
    except AssertionError as error:
        assert "misaligned C callback" in str(error), error
        results.append("negative control: six-register push detected as misaligned")
    else:
        raise AssertionError("negative control was not detected")

    # Run actual capture update twice; no hardware completion/latency is modeled.
    uc = machine()
    for expected_cap, expected_mapping in [(0x80360000, 0x8480), (0x80370000, 0x8084),
                                           (0x80360000, 0x8480)]:
        run("emu_vblankIrq", "emu_vblankIrqReturn", channels, uc=uc)
        assert struct.unpack("<I", uc.mem_read(0x04000064, 4))[0] == expected_cap
        assert struct.unpack("<H", uc.mem_read(0x04000242, 2))[0] == expected_mapping
    results.append("capture register instructions: C/D/C alternation")

    # The actual vm_irq branches dispatch all pending combinations in order.
    dispatch = [(2, "emu_hblankIrq", "emu_hblankIrqReturn"),
                (1 << 16, "emu_arm7Irq", "emu_arm7IrqReturn"),
                (1, "emu_vblankIrq", "emu_vblankIrqReturn")]
    for subset in range(8):
        uc = machine()
        pending = sum(bit for i, (bit, _, _) in enumerate(dispatch) if subset & (1 << i))
        uc.mem_write(0x04000214, struct.pack("<I", pending))
        uc.mem_write(symbols["vm_emulatedIfImeIe"], bytes(4))
        uc.mem_write(symbols["vm_hwIrqMask"], bytes(4))
        uc.mem_write(symbols["vm_cpsr"], struct.pack("<I", 0xD3))
        observed = []

        def dispatch_hook(cpu, pc, size, _):
            for bit, handler, target in dispatch:
                if pc == symbols[handler]:
                    observed.append(handler)
                    cpu.reg_write(UC_ARM_REG_PC, symbols[target])

        uc.hook_add(UC_HOOK_CODE, dispatch_hook)
        uc.emu_start(symbols["vm_irq"], 0x02010000, count=100)
        assert uc.reg_read(UC_ARM_REG_PC) == 0x02010000
        assert observed == [handler for bit, handler, _ in dispatch if pending & bit]
        assert uc.reg_read(REGS[4]) == 0x11220004
        assert uc.reg_read(UC_ARM_REG_CPSR) == 0x6000001F
    results.append("eight pending combinations: HBlank -> ARM7 -> VBlank; exception return and R4 restored")
    report = {"elf": str(args.elf), "irq_stack_end": hex(symbols["dtcmIrqStackEnd"]),
              "callback_call_sites": 11, "results": results, "hardware_timing_tested": False}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
