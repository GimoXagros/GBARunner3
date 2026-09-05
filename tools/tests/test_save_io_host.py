#!/usr/bin/env python3
"""Fault-inject exact Save.cpp functions and report known defects explicitly."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KNOWN_FAILURES = {
    *("deferred_" + f for f in ("seek", "write", "short-write", "sync", "full", "readonly")),
    *("retry_" + f for f in ("seek", "write", "short-write", "sync", "full", "readonly")),
    "interrupted_initialization_retry_fill", "flush_failure_visible",
}
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
    exe = tmp / ("save-io.exe" if os.name == "nt" else "save-io")
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-Wall", "-Wextra", "-fpermissive",
               "-I", str(tmp), "-I", str(ROOT / "code/core/common"),
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
    print(f"{len(results) - len(failed)} PASS, {len(failed)} explicitly tracked expected failures; production is NOT fixed")
