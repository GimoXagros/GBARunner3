#!/usr/bin/env python3
"""Build and verify the stable v0.1.5 release from one exact checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[2]
GIT = ["git", "-c", f"safe.directory={ROOT}"]
TAG = "custom-v0.1.5"
RTC_SOURCE = "22fb48dfeca7575804476abf5f1a5c2a707ae606"
DEVELOP_BASE = "55dac6b0998c4900170e2764b0bb242f91e9b3f5"
TOOLCHAIN = "devkitpro/devkitarm:20241104"
EXPECTED_NDS_SHA256 = "96b9db9efdc70aa43bb582078bf4dfc944826156e13365c8466119c6a507b7a4"
EXPECTED_TEST_NDS_SHA256 = "50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7"
CONFIG_COUNT = 304
EXPECTED_CONFIG_MANIFEST_SHA256 = "0ada1a9e67e36a6b9780d65ad6c39f3eb8922c1750691ac8db46f34ec9d078b8"
README = ROOT / "docs/releases/custom-v0.1.5.md"
RTC_GUIDE = ROOT / "docs/releases/RTC-v0.1.5.md"
MIGRATOR = ROOT / "tools/rtc_migrate.py"


def git(*args):
    return subprocess.check_output([*GIT, *args], cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", required=True, choices=("dry-run", "published"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--zip", dest="archive", type=Path, required=True)
    args = parser.parse_args()
    source = git("rev-parse", "HEAD")
    git("merge-base", "--is-ancestor", RTC_SOURCE, "HEAD")
    require(git("merge-base", "--is-ancestor", DEVELOP_BASE, "HEAD") == "", "develop ancestry missing")
    for pr in ("ab59a7e37a0f73cbe06aa1be4e4e964a64a4c1a3", "a46b781dc8a290bf12684390a921c73916612c64", "cee5ea5018013a4edc44eee6b1cff4720dcd27da"):
        git("merge-base", "--is-ancestor", pr, "HEAD")
    if args.mode == "published":
        require(os.environ.get("GITHUB_REF") == f"refs/tags/{TAG}", "wrong release ref")
        require(os.environ.get("V015_RELEASE_TAG") == TAG, "wrong published release tag")
        require(os.environ.get("V015_RELEASE_PRERELEASE") == "false", "release is not stable")
        require(os.environ.get("V015_RELEASE_DRAFT") == "false", "release is still draft")
        require(git("rev-parse", f"refs/tags/{TAG}^{{}}") == source, "tag target differs from checkout")
    nds = ROOT / "code/bootstrap/GBARunner3.nds"
    test_nds = ROOT / "code/test/test.nds"
    require(nds.is_file() and test_nds.is_file(), "application or test NDS missing")
    nds_hash = digest(nds)
    require(nds_hash == EXPECTED_NDS_SHA256, f"NDS hash mismatch: {nds_hash}")
    test_nds_hash = digest(test_nds)
    require(test_nds_hash == EXPECTED_TEST_NDS_SHA256, f"test NDS hash mismatch: {test_nds_hash}")
    subprocess.check_call([sys.executable, str(ROOT / "tools/validate_nds_banner.py"), str(nds)])
    configs = sorted((ROOT / "configs").iterdir())
    require(len(configs) == CONFIG_COUNT and all(p.is_file() and p.suffix == ".json" for p in configs),
            "config count or extension differs")
    config_lines = [f"{digest(path)}  {path.name}\n" for path in configs]
    config_manifest = hashlib.sha256("".join(config_lines).encode("utf-8")).hexdigest()
    require(config_manifest == EXPECTED_CONFIG_MANIFEST_SHA256,
            f"config manifest mismatch: {config_manifest}")
    require(README.is_file(), "stable release documentation missing")
    require(TAG in README.read_text(encoding="utf-8"), "stable document version mismatch")
    require(RTC_GUIDE.is_file() and MIGRATOR.is_file(), "RTC guide or migration tool missing")
    out = args.out.resolve()
    archive = args.archive.resolve()
    require(not out.exists() or not any(out.iterdir()), "output directory is not empty")
    require(not archive.exists(), "refusing to overwrite package ZIP")
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(nds, out / "GBARunner3.nds")
    for path in configs:
        target = out / "_gba/configs" / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    shutil.copyfile(README, out / "README-v0.1.5.md")
    shutil.copyfile(RTC_GUIDE, out / "RTC-COMPATIBILITY.md")
    (out / "tools").mkdir()
    shutil.copyfile(MIGRATOR, out / "tools/rtc_migrate.py")
    manifest = {
        "release": TAG,
        "channel": "stable",
        "stable_release": TAG,
        "previous_stable_release": "custom-v0.1.4",
        "validated_candidate": "RTC-PR16-L4-22fb48d",
        "validated_candidate_source": RTC_SOURCE,
        "source_commit": source,
        "develop_base": DEVELOP_BASE,
        "toolchain": TOOLCHAIN,
        "arm7_commit_or_submodule": source,
        "libtwl_commit": git("rev-parse", "HEAD:code/libs/libtwl"),
        "nds_sha256": nds_hash,
        "test_nds_sha256": test_nds_hash,
        "configs_count": len(configs),
        "configs_manifest_sha256": config_manifest,
        "hardware_validation": {
            "status": "user-reported-pass",
            "reported_date": "2026-09-30",
            "scope": "normal boot/save; Pokemon Emerald clock set, save, exit, wait, restart and elapsed time observed",
            "device": "3DS + DSpico",
            "exact_wait_duration_and_rom_hash": "not supplied",
            "v015_banner_on_device": "not verified",
            "physical_media_failure_tests": "not verified",
            "pr18_save_error_screen": "not hardware verified; automated verification only",
        },
        "included_prs": [15, 16, 18, 19, 20, 21],
        "excluded_prs": [5, 6],
    }
    (out / "RELEASE-MANIFEST.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    files = sorted(p for p in out.rglob("*") if p.is_file())
    expected = {"GBARunner3.nds", "README-v0.1.5.md", "RTC-COMPATIBILITY.md", "tools/rtc_migrate.py", "RELEASE-MANIFEST.json"}
    expected.update(f"_gba/configs/{p.name}" for p in configs)
    actual = {p.relative_to(out).as_posix() for p in files}
    require(actual == expected, f"package allowlist mismatch: extra={actual - expected}, missing={expected - actual}")
    lines = [f"{digest(path)}  {path.relative_to(out).as_posix()}\n" for path in files]
    sums = out / "SHA256SUMS"
    sums.write_text("".join(lines), encoding="utf-8")
    files.append(sums)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(files):
            info = zipfile.ZipInfo(path.relative_to(out).as_posix(), (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            zf.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(archive) as zf:
        require(set(zf.namelist()) == actual | {"SHA256SUMS"}, "ZIP allowlist differs")
        for line in zf.read("SHA256SUMS").decode("utf-8").splitlines():
            expected_hash, name = line.split("  ", 1)
            require(hashlib.sha256(zf.read(name)).hexdigest() == expected_hash, f"ZIP hash mismatch: {name}")
    print(json.dumps({**manifest, "zip_sha256": digest(archive), "files": len(files)}, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"v0.1.5 packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
