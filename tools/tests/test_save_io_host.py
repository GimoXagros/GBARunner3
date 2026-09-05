#!/usr/bin/env python3
"""Fault-inject exact Save.cpp functions and report known defects explicitly."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KNOWN_FAILURES = set()
parser = argparse.ArgumentParser()
parser.add_argument("--require-fixed", action="store_true", help="fail on every unmet invariant (red baseline)")
parser.add_argument("--output", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="gbar3-save-io-") as tmp:
    tmp = Path(tmp)
    source = (ROOT / "code/core/arm9/source/Save/Save.cpp").read_text(encoding="utf-8")
    scheduler = source[source.index("static DWORD sClusterTable"):source.index("// temporarily")]
    io = source[source.index("static bool loadSaveClusterMap"):]
    (tmp / "production_save_io.h").write_text(scheduler + io, encoding="utf-8")
    (tmp / "GbaSaveIpcService.h").write_text((ROOT / "code/core/arm7/source/IpcServices/GbaSaveIpcService.h").read_text(encoding="utf-8"), encoding="utf-8")
    service = ROOT / "code/core/arm7/source/IpcServices/GbaSaveIpcService.cpp"
    arm7 = service.read_text(encoding="utf-8")
    (tmp / "production_arm7_save.h").write_text(arm7[arm7.index("#define SAVE_WAIT_FRAMES"):], encoding="utf-8")
    arm7_main = (ROOT / "code/core/arm7/source/main.cpp").read_text(encoding="utf-8")
    begin = arm7_main.index("static void updateArm7ExitRequestedState()")
    end = arm7_main.index("static void updateArm7()", begin)
    (tmp / "production_exit.h").write_text(arm7_main[begin:end], encoding="utf-8")
    (tmp / "ThreadIpcService.h").write_text("#pragma once\nclass IpcService { public: IpcService(u32) {} virtual void OnMessageReceived(u32) {} void SendResponseMessage(u32) {} };\n", encoding="utf-8")
    exe = tmp / ("save-io.exe" if os.name == "nt" else "save-io")
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-Wall", "-Wextra", "-fpermissive",
               "-I", str(tmp), "-I", str(ROOT / "code/core/common"),
               "-I", str(ROOT / "code/core/arm7/source/IpcServices"),
               str(ROOT / "tools/tests/save_io_host.cpp"), "-o", str(exe)]
    if os.environ.get("SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    report = subprocess.check_output([str(exe)], text=True)
    results = dict(line.split(":") for line in report.splitlines())
    print(report, end="")
    if args.output:
        args.output.write_text(json.dumps({"results": results, "known_failures": sorted(KNOWN_FAILURES)}, indent=2) + "\n")
    failed = {name for name, value in results.items() if value == "FAIL"}
    assert failed == (set() if args.require_fixed else KNOWN_FAILURES), ("unexpected invariant results", sorted(failed))
    print(f"{len(results) - len(failed)} PASS, {len(failed)} explicitly tracked expected failures; FatFs seam only; physical SD error propagation and concurrency remain separate gates")
