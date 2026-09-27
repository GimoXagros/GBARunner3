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
FATFS = ROOT / "code/core/arm9/source/Fat/ff.c"


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
    sequence_edges = {
        "V2_SEQ_ZERO": 0,
        "V2_SEQ_NEAR_HALF": 0x7FFFFFFE,
        "V2_SEQ_HALF_PRE": 0x7FFFFFFF,
        "V2_SEQ_ROTATED_OLD": 0xFFFFFFFE,
        "V2_SEQ_ROTATED_NEW": 0x7FFFFFFD,
    }
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
        **{name: modern_record(ID_A, sequence=sequence,
                               host_seconds=correct_seconds(clock),
                               game_seconds=correct_seconds(clock))
           for name, sequence in sequence_edges.items()},
    }.items():
        result += array(name, data)
    return result


def production_source(ref: str | None = None) -> str:
    source = (subprocess.check_output(
        ["git", "show", f"{ref}:{RTC.relative_to(ROOT).as_posix()}"],
        cwd=ROOT, text=True,
    ) if ref else RTC.read_text(encoding="utf-8"))
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


def check_fatfs_directory_semantics() -> None:
    """Keep the synthetic directory result tied to bundled FatFs source."""
    source = FATFS.read_text(encoding="utf-8")
    directory = source.index("if (dj.obj.attr & AM_DIR)",
                             source.index("/* Open an existing file */"))
    assert "res = FR_NO_FILE;" in source[directory:directory + 160], \
        "bundled f_open directory behavior changed; update synthetic media seam"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--negative-control", choices=[
        "legacy-auto-load", "legacy-overwrite", "backup-delete",
        "direct-old-delta", "version-bypass", "identity-bypass",
        "uncleared-dirty", "repeat-migration", "no-dirty", "partial-offset",
        "prospective-select-bypass", "missing-directory-guard",
    ])
    parser.add_argument("--baseline-ref", help="read-only production RTC source revision")
    parser.add_argument("--baseline-control", choices=[
        "prospective-select-bypass", "missing-directory-guard",
    ])
    args = parser.parse_args()
    assert not (args.baseline_ref and args.negative_control)
    assert bool(args.baseline_ref) == bool(args.baseline_control)
    check_boot_caller()
    check_fatfs_directory_semantics()
    with tempfile.TemporaryDirectory(prefix="gbar3-rtc-v2-host-") as directory:
        temporary = Path(directory)
        production = production_source(args.baseline_ref)
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
        elif args.negative_control == "legacy-auto-load":
            old = "return LoadStatus::LegacyFound;"
            assert production.count(old) == 1
            production = production.replace(old, "return LoadStatus::FreshInitialized;")
        elif args.negative_control == "direct-old-delta":
            marker = "ready.sequence = 1;"
            assert production.count(marker) == 1
            production = production.replace(marker, marker + "\n" +
                "ready.rtcSecondsSince2000 = (ready.rtcSecondsSince2000 + "
                "ready.hostSecondsSince2000 - sLegacyRecords[ready.selectedLegacyRole]."
                "hostSecondsSince2000) % RtcPersistence::CYCLE_SECONDS;")
        elif args.negative_control == "backup-delete":
            begin = production.index("RTC_EWRAM bool RomGpioRtc::WriteStateFile(")
            end = production.index("RTC_EWRAM bool RomGpioRtc::FlushStateIfDirty()")
            body = production[begin:end]
            assert body.count("return true;") == 1
            production = production[:begin] + body.replace(
                "return true;", "f_unlink(_legacyPaths[2]);\n    return true;",
            ) + production[end:]
        elif args.negative_control == "uncleared-dirty":
            begin = production.index("RTC_EWRAM bool RomGpioRtc::FlushStateIfDirty()")
            end = production.index("RTC_EWRAM void RomGpioRtc::MarkStateDirty()")
            body = production[begin:end]
            assert body.count("_stateDirty = false;") == 1
            production = production[:begin] + body.replace("_stateDirty = false;", "") + production[end:]
        elif args.negative_control == "repeat-migration":
            old = "if (_currentRecord.phase == RtcPersistence::PHASE_PENDING)"
            assert production.count(old) == 1
            production = production.replace(old, "if (true)")
        elif args.negative_control == "prospective-select-bypass":
            old = "if (state.sequence == oldSequence ||\n            !RtcPersistence::IsSequenceNewer(state.sequence, oldSequence))"
            assert production.count(old) == 1
            production = production.replace(old, "if (false)")
        elif args.negative_control == "missing-directory-guard":
            old = "const FRESULT stat = f_stat(path, nullptr);\n        return stat == FR_NO_FILE ? FileStatus::Missing\n            : stat == FR_OK ? FileStatus::Corrupt : FileStatus::IoError;"
            assert production.count(old) == 1
            production = production.replace(old, "return FileStatus::Missing;")
        elif args.negative_control in ("version-bypass", "identity-bypass"):
            begin = production.index("RTC_EWRAM RtcPersistence::FileStatus RomGpioRtc::ReadModernStateFile(")
            end = production.index("RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::ScanLegacy()")
            body = production[begin:end]
            if args.negative_control == "version-bypass":
                old = "if (foundMagic == magic && foundVersion != version)"
                assert production.count(old) == 1
                production = production.replace(old, "if (false)")
                begin = production.index("RTC_EWRAM RtcPersistence::FileStatus RomGpioRtc::ReadModernStateFile(")
                end = production.index("RTC_EWRAM RtcPersistence::LoadStatus RomGpioRtc::ScanLegacy()")
                body = production[begin:end]
            else:
                old = "if (state.gameCode != _identity.gameCode || state.romSize != _identity.romSize ||\n        state.headerHash != _identity.headerHash) return FileStatus::IdentityMismatch;"
                assert old in body
                body = body.replace(old, "")
            fallback = "return RtcPersistence::ValidateV2(state, _identity)\n        ? FileStatus::ValidCurrent : FileStatus::Corrupt;"
            assert fallback in body
            body = body.replace(fallback, "return FileStatus::ValidCurrent;")
            production = production[:begin] + body + production[end:]
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
        control = args.negative_control or args.baseline_control
        run = subprocess.run([str(executable), *(["--negative-control", control]
                                             if control else [])],
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             timeout=30)
        print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="", file=__import__("sys").stderr)
        if control:
            expected = {
                "legacy-auto-load": "legacy-only blocks before guest",
                "legacy-overwrite": "pending original and v1 triad unchanged",
                "backup-delete": "pending original and v1 triad unchanged",
                "direct-old-delta": "adopted GPIO exposes stored snapshot",
                "version-bypass": "unknown modern version blocks",
                "identity-bypass": "ROM identity mismatch blocks",
                "uncleared-dirty": "verified flush clears dirty",
                "repeat-migration": "recreated object reloads ready without migrating twice",
                "no-dirty": "GPIO write persists offset",
                "partial-offset": "incomplete command does not commit offset",
                "prospective-select-bypass": "half-cycle boundary flush preserves reboot selection",
                "missing-directory-guard": "existing RTC directory blocks startup",
            }[control]
            assert run.returncode != 0 and "FAIL " + expected in run.stdout, \
                "negative control did not reach its intended assertion"
            print("PASS baseline rejected" if args.baseline_ref else
                  "PASS negative control rejected", control)
        else:
            run.check_returncode()


if __name__ == "__main__":
    main()
