#!/usr/bin/env python3
"""Unit tests for release packaging memory telemetry, portal release sync, and CLI installer."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


def load_module(rel_path: str, name: str):
    spec = importlib.util.spec_from_file_location(name, REPO_ROOT / rel_path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


class TestReleaseAndInstaller(unittest.TestCase):
    def test_installer_enforces_4mb_limit_and_chip_probe(self) -> None:
        inst = load_module("tools/install_firmware.py", "install_firmware")

        chip, flash_mb = inst.parse_esptool_probe(
            "Detecting chip type... ESP32-D0WD-V3\nManufacturer: c8\nDetected flash size: 4MB\n"
        )
        self.assertEqual(chip, "esp32")
        self.assertEqual(flash_mb, 4)

        recs = [r.env for r in inst.recommend_envs("esp32", 4)]
        self.assertIn("m5stack-cplus1_1", recs)
        self.assertIn("LAUNCHER_m5stack-cplus1_1", recs)
        self.assertNotIn("m5stack-core2", recs)
        self.assertNotIn("m5stack-cplus2", recs)
        self.assertNotIn("lilygo-t-deck", recs)

        with tempfile.TemporaryDirectory() as td:
            tdp = Path(td)
            small_bin = tdp / "NezhaCyber-m5stack-cplus1_1.bin"
            small_bin.write_bytes(b"\x00" * (3 * 1024 * 1024))
            ok, msg = inst.validate_binary_fit(
                small_bin, "m5stack-cplus1_1", detected_flash_mb=4, detected_chip="esp32"
            )
            self.assertTrue(ok, msg)

            # Oversized binary (>4MB) must be rejected on 4MB M5StickC Plus 1.1
            big_bin = tdp / "NezhaCyber-m5stack-cplus1_1.bin"
            big_bin.write_bytes(b"\x00" * (4 * 1024 * 1024 + 4096))
            ok_big, _ = inst.validate_binary_fit(
                big_bin, "m5stack-cplus1_1", detected_flash_mb=4, detected_chip="esp32"
            )
            self.assertFalse(ok_big)

            # 16MB firmware must be rejected on 4MB ESP32
            ok_tier, _ = inst.validate_binary_fit(
                small_bin, "m5stack-core2", detected_flash_mb=4, detected_chip="esp32"
            )
            self.assertFalse(ok_tier)

        sample_rels = [
            {"tag_name": "v2.2.0-beta.1", "prerelease": True},
            {"tag_name": "v2.1.0", "prerelease": False},
            {"tag_name": "v2.0.0", "prerelease": False},
        ]
        self.assertEqual(inst.select_release(sample_rels, "latest", "stable")["tag_name"], "v2.1.0")
        self.assertEqual(inst.select_release(sample_rels, "beta", "beta")["tag_name"], "v2.2.0-beta.1")
        self.assertEqual(inst.select_release(sample_rels, "v2.0.0", "all")["tag_name"], "v2.0.0")

    def test_package_release_emits_memory_and_beta_notes(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tdp = Path(td)
            fbin = tdp / "NezhaCyber-m5stack-cplus1_1.bin"
            obin = tdp / "NezhaCyber-m5stack-cplus1_1-ota.bin"
            fbin.write_bytes(b"\xAA" * (3_800_000))
            obin.write_bytes(b"\xBB" * (3_720_000))
            meta = {
                "env": "m5stack-cplus1_1",
                "board": "M5StickC Plus 1.1",
                "mcu": "esp32",
                "flash": "4MB",
                "flash_mb": 4,
                "psram": "None",
                "edition": "compact",
                "partition": str(REPO_ROOT / "partitions" / "custom_4Mb_full.csv"),
                "app_partition_bytes": 0x3E8000,
                "ram_used_bytes": 96256,
                "ram_total_bytes": 327680,
                "factory_bin": fbin.name,
                "ota_bin": obin.name,
            }
            (tdp / "meta-m5stack-cplus1_1.json").write_text(json.dumps(meta), encoding="utf-8")

            notes_out = tdp / "RELEASE_NOTES.md"
            subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "scripts/build/package_release.py"),
                    "--assets-dir",
                    str(tdp),
                    "--tag",
                    "v2.2.0-beta.1",
                    "--notes-out",
                    str(notes_out),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            manifest = json.loads((tdp / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["channel"], "beta")
            b = manifest["boards"][0]
            self.assertEqual(b["flash_mb"], 4)
            self.assertTrue(b["fits_4mb"])
            self.assertTrue(b["fits_flash"])
            self.assertGreater(b["flash_usage_pct"], 80.0)
            notes = notes_out.read_text(encoding="utf-8")
            self.assertIn("Beta / Pre-release", notes)
            self.assertIn("Compact (<4MB)", notes)
            self.assertIn("94 / 320 KB", notes)

    def test_sync_portal_releases_builds_multi_channel_entry(self) -> None:
        sync = load_module("tools/sync_portal_releases.py", "sync_portal_releases")
        gh_rel = {
            "tag_name": "v2.2.0-beta.1",
            "prerelease": True,
            "published_at": "2026-11-28T10:00:00Z",
            "html_url": "https://github.com/NezhaCyber/nezha_cyber_firmware/releases/tag/v2.2.0-beta.1",
            "body": "# Beta preview",
            "assets": [
                {
                    "name": "NezhaCyber-m5stack-cplus1_1.bin",
                    "size": 3790000,
                    "browser_download_url": "https://example.com/NezhaCyber-m5stack-cplus1_1.bin",
                },
                {
                    "name": "NezhaCyber-LAUNCHER_m5stack-cplus1_1.bin",
                    "size": 2100000,
                    "browser_download_url": "https://example.com/NezhaCyber-LAUNCHER_m5stack-cplus1_1.bin",
                },
            ],
        }
        entry = sync.build_portal_release(
            gh_rel, None, {"NezhaCyber-m5stack-cplus1_1.bin"}, seen_first_stable=False
        )
        self.assertEqual(entry["channel"], "beta")
        self.assertEqual(len(entry["builds"]), 2)
        self.assertEqual(entry["builds"][0]["edition"], "compact")
        self.assertTrue(entry["builds"][0]["staticReady"])
        self.assertEqual(entry["builds"][1]["edition"], "lite")
        self.assertFalse(entry["builds"][1]["staticReady"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
