#!/usr/bin/env python3
"""Exercise the production stable packager with tiny, isolated source fixtures.

Only artifact identities and Git provenance are substituted. Packaging, metadata
gates, filesystem refusal, manifest generation and ZIP verification run unchanged.
The pinned CI dry-run separately validates real build artifacts and Git history.
"""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


SCRIPT = Path(__file__).resolve().parents[2] / ".github/scripts/package-v014.py"
SOURCE = "1" * 40
LIBTWL = "2" * 40


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class StablePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        spec = importlib.util.spec_from_file_location("v014_packager_fixture", SCRIPT)
        self.package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.package)
        self.package.ROOT = self.root
        self.package.README = self.root / "docs/releases/custom-v0.1.4.md"
        self.inputs = {
            "code/bootstrap/GBARunner3.nds": b"fixture application\x00",
            "code/test/test.nds": b"fixture target tests\x00",
            "configs/AAAA.json": b'{"fixture":1}\n',
            "configs/ZZZZ.json": b'{"fixture":2}\n',
            "docs/releases/custom-v0.1.4.md": b"# custom-v0.1.4\nFixture documentation.\n",
        }
        for name, data in self.inputs.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.package.EXPECTED_NDS_SHA256 = sha256(self.inputs["code/bootstrap/GBARunner3.nds"])
        self.package.EXPECTED_TEST_NDS_SHA256 = sha256(self.inputs["code/test/test.nds"])
        self.package.CONFIG_COUNT = 2
        config_manifest = "".join(
            f"{sha256(self.inputs[name])}  {Path(name).name}\n"
            for name in sorted(self.inputs) if name.startswith("configs/")
        )
        self.package.EXPECTED_CONFIG_MANIFEST_SHA256 = sha256(config_manifest.encode())
        self.out = self.root / "output"
        self.archive = self.root / "package.zip"
        self.tag_target = SOURCE
        self.env = {
            "GITHUB_REF": "refs/tags/custom-v0.1.4",
            "V014_RELEASE_TAG": "custom-v0.1.4",
            "V014_RELEASE_PRERELEASE": "false",
            "V014_RELEASE_DRAFT": "false",
        }

    def fake_git(self, *args):
        if args == ("rev-parse", "HEAD"):
            return SOURCE
        if args == ("rev-parse", "refs/tags/custom-v0.1.4^{}"):
            return self.tag_target
        if args == ("rev-parse", "HEAD:code/libs/libtwl"):
            return LIBTWL
        if args[:2] == ("merge-base", "--is-ancestor") or args[:2] == ("cat-file", "-e"):
            return ""
        raise AssertionError(f"Unexpected Git request: {args}")

    def invoke(self, mode="published"):
        args = [str(SCRIPT), "--mode", mode, "--out", str(self.out), "--zip", str(self.archive)]
        output = io.StringIO()
        with patch.object(self.package, "git", side_effect=self.fake_git), \
                patch.object(self.package.subprocess, "run", return_value=subprocess.CompletedProcess([], 1)), \
                patch.dict(os.environ, self.env, clear=True), \
                patch.object(sys, "argv", args), contextlib.redirect_stdout(output):
            self.package.main()
        return json.loads(output.getvalue())

    def assert_rejected(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.invoke()
        self.assertFalse(self.out.exists(), "invalid inputs must not publish any package files")
        self.assertFalse(self.archive.exists())

    def test_valid_stable_package_has_exact_contents_and_verified_identity(self):
        result = self.invoke()
        expected = {
            "GBARunner3.nds", "README-v0.1.4.md", "RELEASE-MANIFEST.json",
            "SHA256SUMS", "_gba/configs/AAAA.json", "_gba/configs/ZZZZ.json",
        }
        with zipfile.ZipFile(self.archive) as archive:
            self.assertEqual(set(archive.namelist()), expected)
            self.assertEqual(len(archive.namelist()), len(expected))
            self.assertIsNone(archive.testzip())
            sums = dict(line.split("  ", 1)[::-1]
                        for line in archive.read("SHA256SUMS").decode().splitlines())
            self.assertEqual(set(sums), expected - {"SHA256SUMS"})
            for name, digest in sums.items():
                self.assertEqual(sha256(archive.read(name)), digest, name)
            manifest = json.loads(archive.read("RELEASE-MANIFEST.json"))
            self.assertEqual(manifest["release"], "custom-v0.1.4")
            self.assertEqual(manifest["stable_release"], "custom-v0.1.4")
            self.assertEqual(manifest["channel"], "stable")
            self.assertEqual(manifest["source_commit"], SOURCE)
            self.assertEqual(manifest["libtwl_commit"], LIBTWL)
            self.assertEqual(manifest["configs_count"], 2)
            self.assertEqual(manifest["configs_manifest_sha256"], self.package.EXPECTED_CONFIG_MANIFEST_SHA256)
            self.assertEqual(manifest["nds_sha256"], self.package.EXPECTED_NDS_SHA256)
            self.assertEqual(manifest["test_nds_sha256"], self.package.EXPECTED_TEST_NDS_SHA256)
            self.assertEqual(manifest["hardware_validation"]["status"], "user-reported-pass")
            self.assertEqual(manifest["hardware_validation"]["physical_media_failure_tests"], "not verified")
            self.assertEqual(archive.read("GBARunner3.nds"), self.inputs["code/bootstrap/GBARunner3.nds"])
            for config in ("AAAA.json", "ZZZZ.json"):
                self.assertEqual(archive.read(f"_gba/configs/{config}"), self.inputs[f"configs/{config}"])
        self.assertEqual(result["zip_sha256"], sha256(self.archive.read_bytes()))
        self.assertEqual(result["files"], len(expected))

    def test_dry_run_without_release_event_metadata(self):
        self.env = {}
        self.assertEqual(self.invoke(mode="dry-run")["channel"], "stable")

    def test_rejects_wrong_release_ref(self):
        self.env["GITHUB_REF"] = "refs/heads/develop"
        self.assert_rejected("wrong release ref")

    def test_rejects_wrong_release_tag(self):
        self.env["V014_RELEASE_TAG"] = "custom-v0.1.3-rc3"
        self.assert_rejected("wrong published release tag")

    def test_rejects_prerelease(self):
        self.env["V014_RELEASE_PRERELEASE"] = "true"
        self.assert_rejected("release is not stable")

    def test_rejects_draft(self):
        self.env["V014_RELEASE_DRAFT"] = "true"
        self.assert_rejected("release is still draft")

    def test_rejects_tag_target_different_from_checkout(self):
        self.tag_target = "3" * 40
        self.assert_rejected("tag target differs from checkout")

    def test_rejects_changed_application(self):
        (self.root / "code/bootstrap/GBARunner3.nds").write_bytes(b"altered application")
        self.assert_rejected("NDS hash mismatch")

    def test_rejects_changed_test_binary(self):
        (self.root / "code/test/test.nds").write_bytes(b"altered target tests")
        self.assert_rejected("test NDS hash mismatch")

    def test_rejects_changed_config(self):
        (self.root / "configs/AAAA.json").write_bytes(b"{}")
        self.assert_rejected("config manifest mismatch")

    def test_rejects_extra_config_payload(self):
        (self.root / "configs/private.sav").write_bytes(b"private data")
        self.assert_rejected("config count or extension differs")

    def test_rejects_wrong_document_version(self):
        self.package.README.write_text("# custom-v0.1.3\n", encoding="utf-8")
        self.assert_rejected("stable document version mismatch")

    def test_preserves_existing_output_and_refuses_packaging(self):
        self.out.mkdir()
        existing = self.out / "do-not-overwrite"
        existing.write_bytes(b"original")
        with self.assertRaisesRegex(ValueError, "output directory is not empty"):
            self.invoke()
        self.assertEqual(list(self.out.iterdir()), [existing])
        self.assertEqual(existing.read_bytes(), b"original")
        self.assertFalse(self.archive.exists())

    def test_preserves_existing_zip_and_refuses_packaging(self):
        self.archive.write_bytes(b"existing release archive")
        with self.assertRaisesRegex(ValueError, "refusing to overwrite package ZIP"):
            self.invoke()
        self.assertEqual(self.archive.read_bytes(), b"existing release archive")
        self.assertFalse(self.out.exists())


if __name__ == "__main__":
    unittest.main()
