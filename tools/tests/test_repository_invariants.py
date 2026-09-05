#!/usr/bin/env python3
"""Validate repository inputs that are packaged or referenced by releases."""

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_DIR = ROOT / "configs"

TOP_LEVEL_KEYS = {"displaySettings", "runSettings", "gameSettings"}
DISPLAY_KEYS = {
    "gbaScreen", "gbaColorCorrection", "gbaDisplayGamma",
    "gbaScreenBrightness", "enableCenterAndMask", "centerOffsetX",
    "centerOffsetY", "maskWidth", "maskHeight", "borderImage",
}
RUN_KEYS = {
    "enableJit", "jitPatchAddresses", "enableRomICache",
    "enableWramICache", "enableIWramDCache", "enableEWramDCache",
    "forceDSModeArm9Clock", "selfModifyingPatchAddresses", "skipBiosIntro",
}
GAME_KEYS = {"saveType"}
BOOL_RUN_KEYS = RUN_KEYS - {"jitPatchAddresses", "selfModifyingPatchAddresses"}
ADDRESS_KEYS = {"jitPatchAddresses", "selfModifyingPatchAddresses"}
HEX_ADDRESS = re.compile(r"\A(?:0x)?[0-9A-Fa-f]{1,8}\Z")
CONFIG_NAME = re.compile(r"\A[A-Z0-9]{4}[0-9]{2}\.json\Z")
MARKDOWN_LINK = re.compile(r"(?<!!)\[[^]]+\]\(([^)]+)\)")


def load_json_without_duplicates(path: Path):
    duplicates = []

    def reject_duplicate_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                duplicates.append(key)
            result[key] = value
        return result

    value = json.loads(path.read_text(encoding="utf-8"),
                       object_pairs_hook=reject_duplicate_keys)
    if duplicates:
        raise AssertionError(f"duplicate JSON keys: {duplicates}")
    return value


class RepositoryInvariantTests(unittest.TestCase):
    def test_configs_match_the_runtime_schema(self):
        paths = sorted(CONFIG_DIR.glob("*.json"))
        self.assertEqual(304, len(paths))
        for path in paths:
            with self.subTest(path=path.name):
                self.assertRegex(path.name, CONFIG_NAME)
                value = load_json_without_duplicates(path)
                self.assertIsInstance(value, dict)
                self.assertFalse(set(value) - TOP_LEVEL_KEYS)

                display = value.get("displaySettings", {})
                self.assertIsInstance(display, dict)
                self.assertFalse(set(display) - DISPLAY_KEYS)
                if "gbaScreen" in display:
                    self.assertIn(display["gbaScreen"].lower(), {"top", "bottom"})
                if "gbaColorCorrection" in display:
                    self.assertIn(display["gbaColorCorrection"].lower(), {
                        "none", "agb001", "ags101", "oxy001", "ntr001",
                        "usg001", "psp01g", "nswips", "nswole", "vbaemu",
                        "nocash", "mgba01",
                    })
                if "borderImage" in display:
                    self.assertIn(display["borderImage"].lower(),
                                  {"none", "default", "game"})
                for key in {"enableCenterAndMask"} & set(display):
                    self.assertIs(type(display[key]), bool)
                for key in (DISPLAY_KEYS - {"gbaScreen", "gbaColorCorrection",
                                            "borderImage", "enableCenterAndMask"}) & set(display):
                    self.assertIs(type(display[key]), int)

                run = value.get("runSettings", {})
                self.assertIsInstance(run, dict)
                self.assertFalse(set(run) - RUN_KEYS)
                for key in BOOL_RUN_KEYS & set(run):
                    self.assertIs(type(run[key]), bool)
                for key in ADDRESS_KEYS & set(run):
                    self.assertIsInstance(run[key], list)
                    for address in run[key]:
                        self.assertIsInstance(address, str)
                        self.assertRegex(address, HEX_ADDRESS)

                game = value.get("gameSettings", {})
                self.assertIsInstance(game, dict)
                self.assertFalse(set(game) - GAME_KEYS)
                if "saveType" in game:
                    self.assertIsInstance(game["saveType"], str)
                    self.assertIn(game["saveType"].lower(), {"auto", "none"})

    def test_no_private_or_runtime_payload_is_tracked(self):
        forbidden_suffixes = {
            ".gba", ".gbc", ".gb", ".sav", ".g3diag", ".zip", ".7z",
            ".nds", ".elf",
        }
        offenders = []
        for path in ROOT.rglob("*"):
            if ".git" in path.parts or not path.is_file():
                continue
            lower_name = path.name.lower()
            if path.suffix.lower() in forbidden_suffixes or lower_name in {"bios.bin"}:
                offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual([], offenders)

    def test_production_has_no_title_specific_diagnostic_path(self):
        roots = [ROOT / "code" / "core", ROOT / "code" / "bootstrap", CONFIG_DIR]
        pattern = re.compile(
            r"B8CJ|Kingdom Hearts|09ED35|09EE76|SAVE SLOT|g3diag|G3DG|G3CF|"
            r"GBAR3_RUNTIME_DIAGNOSTICS", re.IGNORECASE)
        offenders = []
        for base in roots:
            for path in base.rglob("*"):
                if path.is_file():
                    try:
                        text = path.read_text(encoding="utf-8")
                    except UnicodeDecodeError:
                        continue
                    if pattern.search(text):
                        offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual([], offenders)

    def test_markdown_relative_links_resolve(self):
        broken = []
        for path in ROOT.rglob("*.md"):
            if ".git" in path.parts:
                continue
            text = path.read_text(encoding="utf-8")
            for match in MARKDOWN_LINK.finditer(text):
                target = match.group(1).strip().split("#", 1)[0]
                if not target or "://" in target or target.startswith(("mailto:", "/")):
                    continue
                target = target.split(" ", 1)[0].strip("<>")
                if not (path.parent / target).exists():
                    broken.append(f"{path.relative_to(ROOT)} -> {target}")
        self.assertEqual([], broken)


if __name__ == "__main__":
    unittest.main()
