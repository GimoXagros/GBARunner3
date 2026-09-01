#!/usr/bin/env python3
"""Guard the high-ROM undefined dispatcher against ARM/Thumb state loss."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "code/core/arm9/source/MemoryEmulator/HiCodeCacheMapping.s"


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    block = source.split("notHicodeMiss:", 1)[1].split(".bss", 1)[0]

    mode_switch = block.index("msr cpsr_c, #0xD1")
    saved_state_load = block.index(
        "ldr r10, [r8, #(vm_undefinedSpsr - vm_undefinedInstructionAddr)]"
    )
    state_test = block.index("tst r10, #0x20")
    cache_fetch = block.index("mrc p15, 3, lr, c15, c3, 0")
    arm_dispatch = block.index("beq vm_undefinedArmInstructionInLR")
    halfword_select = block.index("moveq lr, lr, lsr #16")
    thumb_dispatch = block.index("b vm_undefinedThumbInstructionInLR")

    assert mode_switch < saved_state_load < state_test < cache_fetch < arm_dispatch
    assert arm_dispatch < halfword_select < thumb_dispatch
    assert block.count("vm_undefinedThumbInstructionInLR") == 1
    assert "tst r13, #0x20" not in block

    vm_source = (
        ROOT / "code/core/arm9/source/VirtualMachine/VMUndefined.s"
    ).read_text(encoding="utf-8")
    thumb_entry = vm_source.index("arm_func vm_undefinedThumb\n")
    thumb_loaded_entry = vm_source.index(
        "arm_func vm_undefinedThumbInstructionInLR\n"
    )
    thumb_table_load = vm_source.index(
        "ldr r10, DTCM(vm_undefinedSpsr)", thumb_loaded_entry
    )
    assert thumb_entry < thumb_loaded_entry < thumb_table_load

    print("high-ROM undefined dispatch test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
