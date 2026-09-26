#!/usr/bin/env python3
"""Compile production RTC loader/writer/GPIO against synthetic FatFs media.

The production RTC method bodies are extracted because libtwl and NDS hardware
headers cannot be linked into a host executable. Only FatFs and the DS-clock
IPC call are replaced. This is a host seam, not physical FAT durability or a
hardware run. The linked ARM build is checked separately by the build owner.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import subprocess
import tempfile

from rtc_legacy_fixtures import (
    ID_A, ID_B, correct_seconds, legacy_record_from_old_clock,
    modern_record,
)

ROOT = Path(__file__).resolve().parents[2]
RTC = ROOT / "code/core/arm9/source/Peripherals/RomGpio/RomGpioRtc.cpp"
GPIO = ROOT / "code/core/arm9/source/Peripherals/RomGpio/RomGpio.cpp"


def array(name: str, data: bytes) -> str:
    return f"const u8 {name}[] = {{" + ",".join(str(x) for x in data) + "};\n"


def fixture_source() -> str:
    clock = datetime(2024, 2, 28, 22, 45, 45)
    game = datetime(2024, 2, 28, 21, 39, 39)
    legacy = tuple(legacy_record_from_old_clock(
        ID_A, sequence=sequence, host=clock, game=game,
    ) for sequence in (7, 8, 6))
    selected_game = int.from_bytes(legacy[1][28:32], "little")
    pending = modern_record(ID_A, sequence=0, host_seconds=0,
                            game_seconds=selected_game, legacy=legacy,
                            selected_role=1, pending=True)
    ready = modern_record(ID_A, sequence=0, host_seconds=correct_seconds(clock),
                          game_seconds=selected_game, legacy=legacy,
                          selected_role=1)
    fresh = modern_record(ID_A, sequence=7, host_seconds=correct_seconds(clock),
                          game_seconds=correct_seconds(clock))
    wrapped = modern_record(ID_A, sequence=0, host_seconds=correct_seconds(clock),
                            game_seconds=correct_seconds(clock) + 60)
    old_wrap = modern_record(ID_A, sequence=0xFFFFFFFF,
                             host_seconds=correct_seconds(clock),
                             game_seconds=correct_seconds(clock))
    half_cycle = modern_record(ID_A, sequence=0x80000000,
                               host_seconds=correct_seconds(clock),
                               game_seconds=correct_seconds(clock))
    cycle_mid = modern_record(ID_A, sequence=0x70000000,
                              host_seconds=correct_seconds(clock),
                              game_seconds=correct_seconds(clock))
    cycle_last = modern_record(ID_A, sequence=0xE0000000,
                               host_seconds=correct_seconds(clock),
                               game_seconds=correct_seconds(clock))
    equal_different = modern_record(ID_A, sequence=7,
                                    host_seconds=correct_seconds(clock),
                                    game_seconds=correct_seconds(clock) + 1)
    future = bytearray(fresh)
    future[4:6] = (3).to_bytes(2, "little")
    # Header metadata changes need an otherwise valid checksum to distinguish
    # unsupported version from checksum corruption.
    from rtc_legacy_fixtures import fnv1a
    future[196:200] = fnv1a(future[:196]).to_bytes(4, "little")
    result = f"constexpr u32 FIXTURE_HOST={correct_seconds(clock)}u;\n"
    for name, data in {
        "V1_PRIMARY": legacy[0], "V1_TEMP": legacy[1], "V1_BACKUP": legacy[2],
        "V2_PENDING": pending, "V2_READY": ready, "V2_FRESH": fresh,
        "V2_WRAP": wrapped, "V2_OLD_WRAP": old_wrap, "V2_FUTURE": future,
        "V2_HALF_CYCLE": half_cycle, "V2_CYCLE_MID": cycle_mid,
        "V2_CYCLE_LAST": cycle_last, "V2_EQUAL_DIFFERENT": equal_different,
    }.items():
        result += array(name, data)
    return result


def production_source() -> str:
    source = RTC.read_text(encoding="utf-8")
    begin = source.index("#define RTC_EWRAM")
    rtc = source[begin:]
    clock_begin = rtc.index("RTC_EWRAM void RomGpioRtc::UpdateDSDateTime()")
    clock_end = rtc.index("RTC_EWRAM void RomGpioRtc::UpdateDateTime()")
    rtc = rtc[:clock_begin] + rtc[clock_end:]
    gpio = GPIO.read_text(encoding="utf-8")
    gpio_methods = gpio[gpio.index("void RomGpio::Initialize("):
                        gpio.index("static void updateRomGpioPeripherals()")]
    return rtc + "\n" + gpio_methods


def check_boot_caller() -> None:
    """Source-order guard for the real boot caller, separate from host I/O seam."""
    boot = (ROOT / "code/core/arm9/source/main.cpp").read_text(encoding="utf-8")
    load = boot.index("gRomGpio.LoadRtcState(")
    save = boot.index("handleSave(savePath.get())")
    vm = boot.index("VirtualMachine virtualMachine")
    assert load < save < vm, "RTC load/gate must precede save mutation and guest VM"
    guard = boot[load:save]
    assert "LoadStatus::Ready" in guard and "LoadStatus::FreshInitialized" in guard, \
        "boot must admit only ready or fresh RTC state"
    assert "rtc_persistenceFault(rtcLoadStatus)" in guard, \
        "blocked RTC state must stop boot before save/VM"
    assert all(name in boot[:load] for name in (".g3rtc2", ".g3rtc2.tmp", ".g3rtc2.bak")), \
        "production caller must build all three modern sidecar paths"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--negative-control", choices=[
        "legacy-auto-load", "uncleared-dirty", "no-dirty", "partial-offset",
    ])
    args = parser.parse_args()
    check_boot_caller()
    with tempfile.TemporaryDirectory(prefix="gbar3-rtc-v2-host-") as directory:
        temporary = Path(directory)
        production = production_source()
        if args.negative_control == "no-dirty":
            begin = production.index("RTC_EWRAM void RomGpioRtc::UpdateRtcOffset()")
            end = production.index("RTC_EWRAM void RomGpioRtc::SetYear(")
            body = production[begin:end]
            assert body.count("MarkStateDirty();") == 1
            production = production[:begin] + body.replace("MarkStateDirty();", "") + production[end:]
        elif args.negative_control == "partial-offset":
            assert production.count("if (_offsetUpdateRequired)") == 1
            production = production.replace("if (_offsetUpdateRequired)",
                                            "if (_offsetUpdateRequired || _byteIndex == 2)")
        (temporary / "production_rtc.h").write_text(production, encoding="utf-8")
        (temporary / "rtc_v2_fixtures.h").write_text(fixture_source(), encoding="utf-8")
        executable = temporary / ("rtc-v2.exe" if os.name == "nt" else "rtc-v2")
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-g",
                   "-Wall", "-Wextra", "-I", str(temporary),
                   "-I", str(ROOT / "code/core/arm9/source"),
                   str(ROOT / "tools/tests/rtc_v2_host.cpp"), "-o", str(executable)]
        if os.environ.get("SANITIZE") == "1":
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        run = subprocess.run([str(executable), *(["--negative-control", args.negative_control]
                                             if args.negative_control else [])],
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             timeout=30)
        print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="", file=__import__("sys").stderr)
        if args.negative_control:
            expected = {
                "legacy-auto-load": "legacy-only blocks before guest",
                "uncleared-dirty": "verified flush clears dirty",
                "no-dirty": "GPIO write persists offset",
                "partial-offset": "incomplete command does not commit offset",
            }[args.negative_control]
            assert run.returncode != 0 and "FAIL " + expected in run.stdout, \
                "negative control did not reach its intended assertion"
            print("PASS negative control rejected", args.negative_control)
        else:
            run.check_returncode()


if __name__ == "__main__":
    main()
