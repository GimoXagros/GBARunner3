#!/usr/bin/env python3
"""Execute the application ELF's linked ARM9 RTC loader on synthetic media.

Unicorn runs production ARM instructions. Hooks provide FatFs calls and a
corrected DS RTC reply. This verifies loader reachability and machine-code
execution, but cannot establish physical FAT durability or ARM7 IPC timing.
Dependencies: unicorn==2.1.4, pyelftools==0.32.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timedelta
from pathlib import Path
import struct

from unicorn import (
    Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE,
    UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE,
)
from unicorn.arm_const import (
    UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
    UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
)

from rtc_legacy_fixtures import (
    ID_A, correct_seconds, legacy_record_from_old_clock, modern_record,
)
from test_hicode_dispatch_elf import load_elf

FR_OK, FR_DISK_ERR, FR_NO_FILE, FR_EXIST = 0, 1, 4, 8
FA_WRITE, FA_CREATE_NEW, FA_CREATE_ALWAYS = 2, 4, 8
SENTINEL = 0x0300F000
HEAP_BASE = 0x023E0000
STACK = 0x023D0000
STATUS_READY, STATUS_FRESH, STATUS_LEGACY = 0, 1, 2
STATUS_CORRUPT, STATUS_CONFLICT = 3, 7


def bcd(value: int) -> int:
    return (value // 10 << 4) | value % 10


def find_symbol(symbols: dict[str, int], fragment: str) -> int:
    matches = [(name, address) for name, address in symbols.items()
               if fragment in name and address and not name.startswith("__")]
    exact = [(name, address) for name, address in matches if "veneer" not in name]
    if len(exact) != 1:
        raise AssertionError(f"expected one linked symbol containing {fragment}: {exact}")
    return exact[0][1]


class LinkedRtc:
    def __init__(self, symbols: dict[str, int], sections: list[tuple[int, bytes]]):
        self.symbols = symbols
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for base, size in (
            (0, 0x8000), (0x02000000, 0x400000), (0x03000000, 0x10000),
            (0x037C0000, 0x4000), (0x03800000, 0x10000),
            (0x04000000, 0x2000), (0x04100000, 0x1000),
            (0x06800000, 0x100000),
            (0xFFFF8000, 0x8000),
        ):
            self.cpu.mem_map(base, size)
        for address, data in sections:
            self.cpu.mem_write(address, data)
        self.file_addresses = {name: HEAP_BASE + 0x100 + i * 0x20
                               for i, name in enumerate(("l0", "l1", "l2", "m0", "m1", "m2"))}
        for name, address in self.file_addresses.items():
            self.cpu.mem_write(address, name.encode() + b"\0")
        self.cpu.mem_write(HEAP_BASE + 0x300, struct.pack("<III", *ID_A))
        self.media: dict[str, bytes] = {}
        self.directories: set[str] = set()
        self.handles: dict[int, tuple[str, int, int]] = {}
        self.clock = datetime(2024, 2, 28, 22, 45, 45)
        self.writes = 0
        self.syncs = 0
        self.closed_writes = 0
        self.readbacks = 0
        self.clock_reads = 0
        self.rename_unlink = 0
        self.scheduler_calls = 0
        self.last_invalid = None
        self.bridge_entries: list[tuple[int, int]] = []
        self.stack_faults = 0
        self.entry = find_symbol(symbols, "RomGpioRtc10Initialize")
        self.update = find_symbol(symbols, "RomGpioRtc14UpdateDateTime")
        self.clock_entry = find_symbol(symbols, "RomGpioRtc16UpdateDSDateTime")
        self.clock_buffer = find_symbol(symbols, "RomGpioRtc14sDSRtcDateTime")
        self.hooks = {symbols[name] & ~1: name for name in
                      ("f_open", "f_read", "f_write", "f_sync", "f_close")}
        if "f_stat" in symbols:
            self.hooks[symbols["f_stat"] & ~1] = "f_stat"
        for name in ("f_unlink", "f_rename", "sav_requestFileWrite"):
            if name in symbols:
                self.hooks[symbols[name] & ~1] = name
        self.hooks[self.clock_entry & ~1] = "DS_CLOCK"
        self.work_symbols = {name: symbols[name] for name in (
            "rtc_runOnWorkStack", "rtc_flushOnWorkStackBody",
            "rtc_workStackFault", "rtcWorkStackBase", "rtcWorkStackEnd",
            "rtcWorkStackBusy", "rtcWorkStackHighWater",
        ) if name in symbols}
        self.work_available = len(self.work_symbols) == 7
        if self.work_available:
            self.bridge_entry = self.work_symbols["rtc_flushOnWorkStackBody"] & ~1
            self.hooks[self.work_symbols["rtc_workStackFault"] & ~1] = "STACK_FAULT"
        self.cpu.hook_add(UC_HOOK_CODE, self._hook)
        self.cpu.hook_add(UC_HOOK_MEM_INVALID, self._invalid_memory)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self._ipc_write,
                          begin=0x04000188, end=0x04000188)

    def _supply_ds_clock(self) -> None:
        self.clock_reads += 1
        clock = self.clock
        self.cpu.mem_write(self.clock_buffer, bytes((
            bcd(clock.year - 2000), bcd(clock.month), bcd(clock.day),
            clock.weekday(), bcd(clock.hour) | (0x80 if clock.hour >= 12 else 0),
            bcd(clock.minute), bcd(clock.second),
        )))

    def _ipc_write(self, cpu: Uc, access: int, address: int,
                   size: int, value: int, _: object) -> None:
        # GCC may inline UpdateDSDateTime at a pending-adoption call site.
        # Observe the actual IPC FIFO write and supply the same ARM7 date bytes.
        self._supply_ds_clock()

    def _invalid_memory(self, cpu: Uc, access: int, address: int,
                        size: int, value: int, _: object) -> bool:
        self.last_invalid = (access, address, size,
                             cpu.reg_read(UC_ARM_REG_PC), cpu.reg_read(UC_ARM_REG_LR))
        return False

    def _arg(self, register: int) -> int:
        return self.cpu.reg_read(register)

    def _put32(self, address: int, value: int) -> None:
        self.cpu.mem_write(address, struct.pack("<I", value))

    def _cstring(self, address: int) -> str:
        data = bytearray()
        for i in range(128):
            byte = self.cpu.mem_read(address + i, 1)[0]
            if byte == 0:
                return data.decode("ascii")
            data.append(byte)
        raise AssertionError("unterminated synthetic RTC path")

    def _return(self, result: int = 0) -> None:
        self.cpu.reg_write(UC_ARM_REG_R0, result)
        self.cpu.reg_write(UC_ARM_REG_PC, self.cpu.reg_read(UC_ARM_REG_LR))

    def _hook(self, cpu: Uc, pc: int, size: int, _: object) -> None:
        if self.work_available and pc == self.bridge_entry:
            self.bridge_entries.append((self.cpu.reg_read(UC_ARM_REG_SP),
                                        self.cpu.reg_read(UC_ARM_REG_CPSR)))
        name = self.hooks.get(pc)
        if name is None:
            return
        r0, r1, r2, r3 = (self._arg(register) for register in
                          (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
        if name == "DS_CLOCK":
            self._supply_ds_clock()
            self._return()
        elif name == "STACK_FAULT":
            self.stack_faults += 1
            self.cpu.emu_stop()
        elif name == "f_open":
            path = self._cstring(r1)
            if path in self.directories:
                self._return(FR_NO_FILE); return
            if path.startswith("m") and not (r2 & FA_WRITE) and self.writes:
                self.readbacks += 1
            if r2 & FA_CREATE_NEW:
                if path in self.media:
                    self._return(FR_EXIST); return
                self.media[path] = b""
            elif r2 & FA_CREATE_ALWAYS:
                self.media[path] = b""
            elif path not in self.media:
                self._return(FR_NO_FILE); return
            if self.handles:
                raise AssertionError("linked loader reopened a live FIL")
            self.handles[r0] = (path, 0, r2)
            # FatFs f_size macro reads FFOBJID.objsize, a 32-bit member at +12
            # in this project's FF_FS_EXFAT=0 configuration.
            self._put32(r0 + 12, len(self.media[path]))
            self._return(FR_OK)
        elif name == "f_stat":
            path = self._cstring(r0)
            self._return(FR_OK if path in self.media or path in self.directories
                         else FR_NO_FILE)
        elif name == "f_read":
            path, position, mode = self.handles[r0]
            chunk = self.media[path][position:position + r2]
            if chunk:
                self.cpu.mem_write(r1, chunk)
            self._put32(r3, len(chunk))
            self.handles[r0] = (path, position + len(chunk), mode)
            self._return(FR_OK)
        elif name == "f_write":
            path, position, mode = self.handles[r0]
            data = bytes(self.cpu.mem_read(r1, r2))
            current = bytearray(self.media[path])
            if len(current) < position + len(data):
                current.extend(b"\0" * (position + len(data) - len(current)))
            current[position:position + len(data)] = data
            self.media[path] = bytes(current)
            self.handles[r0] = (path, position + len(data), mode)
            self._put32(r3, len(data))
            self.writes += 1
            self._return(FR_OK)
        elif name == "f_sync":
            self.syncs += 1
            self._return(FR_OK)
        elif name == "f_close":
            assert r0 in self.handles, "linked loader closed unknown FIL"
            if self.handles[r0][2] & FA_WRITE:
                self.closed_writes += 1
            del self.handles[r0]
            self._return(FR_OK)
        elif name in ("f_unlink", "f_rename"):
            self.rename_unlink += 1
            self._return(FR_DISK_ERR)
        elif name == "sav_requestFileWrite":
            self.scheduler_calls += 1
            self._return()

    def call(self, address: int, this: int, *, initialize: bool = False) -> int:
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0xD3)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK)
        self.cpu.reg_write(UC_ARM_REG_LR, SENTINEL)
        self.cpu.reg_write(UC_ARM_REG_R0, this)
        if initialize:
            self.cpu.reg_write(UC_ARM_REG_R1, self.file_addresses["l0"])
            self.cpu.reg_write(UC_ARM_REG_R2, self.file_addresses["l1"])
            self.cpu.reg_write(UC_ARM_REG_R3, self.file_addresses["l2"])
            self.cpu.mem_write(STACK, struct.pack("<4I",
                self.file_addresses["m0"], self.file_addresses["m1"],
                self.file_addresses["m2"], HEAP_BASE + 0x300))
        try:
            self.cpu.emu_start(address, SENTINEL, count=2_000_000)
        except UcError as error:
            raise AssertionError(
                f"linked ARM RTC fault {error}; last_invalid={self.last_invalid}; "
                f"PC={self.cpu.reg_read(UC_ARM_REG_PC):#x} "
                f"LR={self.cpu.reg_read(UC_ARM_REG_LR):#x}") from error
        assert self.cpu.reg_read(UC_ARM_REG_PC) == SENTINEL, \
            "linked ARM RTC function exhausted instruction budget"
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def initialize(self, this: int) -> int:
        return self.call(self.entry, this, initialize=True)

    def visible_date_time(self, this: int) -> bytes:
        self.call(self.update, this)
        # ARM AAPCS layout: five u32 transfer fields (20 bytes), two u16
        # registers (4 bytes), then the seven-byte RTC GPIO date/time struct.
        return bytes(self.cpu.mem_read(this + 24, 7))

    def work_stack_call(self, *, busy: bool = False, corrupt_guard: bool = False,
                        irq_masked: bool = False) -> tuple[int, bool]:
        assert self.work_available, "application ELF lacks linked RTC work stack"
        symbol = self.work_symbols
        base, end = symbol["rtcWorkStackBase"], symbol["rtcWorkStackEnd"]
        assert end - base == 2048, "RTC work stack must be exactly 2048 bytes"
        irq_top = 0x0300D000
        self.cpu.mem_write(irq_top - 288, bytes([0x5A] * 288))
        registers = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
                     UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
                     UC_ARM_REG_R10, UC_ARM_REG_R11)
        expected = tuple(0x44550000 + i * 0x1111 for i in range(len(registers)))
        for register, value in zip(registers, expected):
            self.cpu.reg_write(register, value)
        cpsr = 0x93 if irq_masked else 0x13  # SVC with IRQ masked/unmasked
        self.cpu.reg_write(UC_ARM_REG_CPSR, cpsr)
        self.cpu.reg_write(UC_ARM_REG_SP, irq_top)
        self.cpu.reg_write(UC_ARM_REG_LR, SENTINEL)
        if busy:
            self._put32(symbol["rtcWorkStackBusy"], 1)
        if corrupt_guard:
            self._put32(base, 0)
        before_bridge = len(self.bridge_entries)
        before_fault = self.stack_faults
        self.cpu.emu_start(symbol["rtc_runOnWorkStack"], SENTINEL, count=2_000_000)
        faulted = self.stack_faults != before_fault
        if not faulted:
            assert self.cpu.reg_read(UC_ARM_REG_PC) == SENTINEL, \
                "RTC work stack did not return to caller"
            assert self.cpu.reg_read(UC_ARM_REG_SP) == irq_top, \
                "RTC work stack failed to restore caller SP"
            assert tuple(self.cpu.reg_read(register) for register in registers) == expected, \
                "RTC work stack failed to preserve callee-saved registers"
            assert self.cpu.reg_read(UC_ARM_REG_CPSR) & 0x9F == cpsr & 0x9F, \
                "RTC work stack failed to restore caller mode/IRQ mask"
        assert bytes(self.cpu.mem_read(irq_top - 288, 264)) == bytes([0x5A] * 264), \
            "RTC work overflowed simulated 288-byte IRQ stack"
        if not busy and not faulted:
            assert len(self.bridge_entries) == before_bridge + 1, \
                "RTC work wrapper did not call production bridge exactly once"
            callback_sp, callback_cpsr = self.bridge_entries[-1]
            assert base + 4 < callback_sp <= end and callback_sp & 7 == 0, \
                "RTC bridge did not run on aligned bounded EWRAM stack"
            assert callback_cpsr & 0x80 == cpsr & 0x80, \
                "RTC bridge did not inherit original IRQ mask"
        return self.cpu.reg_read(UC_ARM_REG_R0), faulted


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("arm9", type=Path, help="linked application ARM9 ELF")
    parser.add_argument("--allow-old-no-stack-wrapper", action="store_true",
                        help="baseline probe only; final candidate must link RTC work stack")
    args = parser.parse_args()
    symbols, sections = load_elf(args.arm9)
    if not args.allow_old_no_stack_wrapper:
        assert "f_stat" in symbols, "candidate ELF must link RTC directory distinction"
    clock = datetime(2024, 2, 28, 22, 45, 45)
    game = datetime(2024, 2, 28, 21, 39, 39)
    triad = tuple(legacy_record_from_old_clock(
        ID_A, sequence=sequence, host=clock, game=game,
    ) for sequence in (7, 8, 6))
    game_seconds = struct.unpack_from("<I", triad[1], 28)[0]
    pending = modern_record(ID_A, sequence=0, host_seconds=0,
                            game_seconds=game_seconds, legacy=triad,
                            selected_role=1, pending=True)
    ready = modern_record(ID_A, sequence=1,
                          host_seconds=correct_seconds(clock),
                          game_seconds=game_seconds, legacy=triad,
                          selected_role=1)

    fresh = LinkedRtc(symbols, sections)
    assert fresh.initialize(HEAP_BASE + 0x800) == STATUS_FRESH
    assert not fresh.media and fresh.writes == 0 and fresh.clock_reads == 0

    legacy = LinkedRtc(symbols, sections)
    legacy.media.update(zip(("l0", "l1", "l2"), triad))
    before_legacy = dict(legacy.media)
    assert legacy.initialize(HEAP_BASE + 0x800) == STATUS_LEGACY
    assert legacy.media == before_legacy and legacy.writes == 0 and legacy.clock_reads == 0

    adopted = LinkedRtc(symbols, sections)
    adopted.media.update(zip(("l0", "l1", "l2"), triad))
    adopted.media["m0"] = pending
    assert adopted.initialize(HEAP_BASE + 0x800) == STATUS_READY
    assert adopted.media["m0"] == pending and adopted.media["m1"] == ready
    assert adopted.writes == 1 and adopted.syncs == 1 and \
        adopted.closed_writes == 1 and adopted.readbacks >= 1 and not adopted.handles, \
        "linked pending commit must write, sync, close and reread"
    assert all(adopted.media[f"l{i}"] == triad[i] for i in range(3))
    assert adopted.visible_date_time(HEAP_BASE + 0x800) == bytes.fromhex(
        "24 02 28 00 01 39 39"), "linked GPIO date/time after adoption"
    writes = adopted.writes
    adopted.clock += timedelta(seconds=1)
    assert adopted.initialize(HEAP_BASE + 0xA00) == STATUS_READY
    assert adopted.writes == writes, "linked reload must not re-adopt"
    assert adopted.visible_date_time(HEAP_BASE + 0xA00) == bytes.fromhex(
        "24 02 28 00 01 39 40"), "linked GPIO date/time after restart"
    assert adopted.rename_unlink == 0
    adopted.media["l1"] = legacy_record_from_old_clock(
        ID_A, sequence=9, host=clock, game=game)
    assert adopted.initialize(HEAP_BASE + 0xC00) == STATUS_CONFLICT, \
        "downgrade-changed legacy bytes must block re-upgrade"
    assert adopted.writes == writes and adopted.rename_unlink == 0

    if "f_stat" in symbols:
        for slot in ("l2", "m2"):
            directory = LinkedRtc(symbols, sections)
            directory.directories.add(slot)
            assert directory.initialize(HEAP_BASE + 0x800) == STATUS_CORRUPT, \
                "linked loader must reject an RTC path naming a directory"
            assert directory.writes == 0 and not directory.media

        boundary = LinkedRtc(symbols, sections)
        boundary.media["m0"] = modern_record(
            ID_A, sequence=0, host_seconds=correct_seconds(clock),
            game_seconds=correct_seconds(clock))
        boundary.media["m1"] = modern_record(
            ID_A, sequence=0x7FFFFFFF, host_seconds=correct_seconds(clock),
            game_seconds=correct_seconds(clock))
        static_boundary_rtc = find_symbol(symbols, "sRomGpioRtc")
        mark_boundary_dirty = find_symbol(symbols, "RomGpioRtc14MarkStateDirty")
        assert boundary.initialize(static_boundary_rtc) == STATUS_READY
        boundary.call(mark_boundary_dirty, static_boundary_rtc)
        result, faulted = boundary.work_stack_call()
        assert result == 1 and not faulted and boundary.writes == 1 and \
            "m2" not in boundary.media, \
            "linked writer must replace old sequence before half-cycle conflict"
        assert boundary.initialize(HEAP_BASE + 0xA00) == STATUS_READY, \
            "linked half-cycle journal must remain readable after reboot"

    stacked = LinkedRtc(symbols, sections)
    if not stacked.work_available:
        assert args.allow_old_no_stack_wrapper, \
            "candidate application ELF must link RTC work-stack wrapper and guards"
    else:
        static_rtc = find_symbol(symbols, "sRomGpioRtc")
        mark_dirty = find_symbol(symbols, "RomGpioRtc14MarkStateDirty")
        assert stacked.initialize(static_rtc) == STATUS_FRESH
        stacked.call(mark_dirty, static_rtc)
        assert stacked.scheduler_calls == 1
        result, faulted = stacked.work_stack_call()
        assert result == 1 and not faulted
        expected_fresh = modern_record(
            ID_A, sequence=0, host_seconds=correct_seconds(clock),
            game_seconds=correct_seconds(clock), weekday_offset=0,
            status=0x40, interrupt=0,
        )
        assert stacked.media.get("m0") == expected_fresh, \
            "RTC wrapper must execute production flush on synthetic media"
        assert stacked.writes == 1 and stacked.syncs == 1 and \
            stacked.closed_writes == 1 and stacked.readbacks >= 1, \
            ("linked work-stack commit I/O", stacked.writes, stacked.syncs,
             stacked.closed_writes, stacked.readbacks)
        high_water = struct.unpack("<I", stacked.cpu.mem_read(
            stacked.work_symbols["rtcWorkStackHighWater"], 4))[0]
        assert 8 < high_water < 2048, "RTC work-stack high-water outside bounds"
        before_writes, before_bridge = stacked.writes, len(stacked.bridge_entries)
        result, faulted = stacked.work_stack_call(busy=True, irq_masked=True)
        assert result == 0 and not faulted and stacked.writes == before_writes and \
            len(stacked.bridge_entries) == before_bridge, \
            "busy guard must reject reentry without a second flush"
        stacked._put32(stacked.work_symbols["rtcWorkStackBusy"], 0)
        result, faulted = stacked.work_stack_call(corrupt_guard=True)
        assert faulted and stacked.stack_faults == 1 and \
            stacked.writes == before_writes, \
            "damaged guard must enter terminal fault before FatFs writes"

    stack_result = (f"work-stack guards PASS, successful-flush high-water {high_water} bytes"
                    if stacked.work_available else "work-stack guards NOT RUN")
    stat_result = "directory and sequence boundary PASS" if "f_stat" in symbols else \
        "directory and sequence boundary NOT RUN"
    print("linked ARM9 RTC loader: fresh, legacy gate, pending commit/readback, "
          f"restart GPIO, conflict, {stat_result}, {stack_result}; synthetic FatFs/DS clock, "
          "backend callee stack excluded; hardware NOT RUN")


if __name__ == "__main__":
    main()
