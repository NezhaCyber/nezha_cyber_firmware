#!/usr/bin/env python3
"""Repository & firmware invariant unit tests for Nezha Cyber."""

from __future__ import annotations

import csv
import py_compile
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


class TestFirmwareInvariants(unittest.TestCase):
    def test_partition_tables_valid_and_non_overlapping(self) -> None:
        part_files = sorted((REPO_ROOT / "partitions").glob("*.csv"))
        self.assertGreaterEqual(len(part_files), 4, "Expected at least 4 partition tables")

        for pfile in part_files:
            with self.subTest(partition=pfile.name):
                regions: list[tuple[int, int, str]] = []
                cursor = 0x9000
                for raw_line in pfile.read_text(encoding="utf-8").splitlines():
                    line = raw_line.strip()
                    if not line or line.startswith("#"):
                        continue
                    cols = [c.strip() for c in next(csv.reader([line]))]
                    self.assertGreaterEqual(len(cols), 5, f"Malformed row in {pfile.name}: {line}")
                    name, _, _, off_s, size_s = cols[:5]

                    def parse_num(s: str) -> int:
                        s_up = s.upper()
                        if s_up.endswith("K"):
                            return int(s_up[:-1], 0) * 1024
                        if s_up.endswith("M"):
                            return int(s_up[:-1], 0) * 1024 * 1024
                        return int(s, 0)

                    size = parse_num(size_s)
                    self.assertGreater(size, 0, f"Zero size partition {name} in {pfile.name}")
                    offset = parse_num(off_s) if off_s else cursor
                    end = offset + size
                    for prev_s, prev_e, prev_n in regions:
                        overlap = not (end <= prev_s or offset >= prev_e)
                        self.assertFalse(
                            overlap,
                            f"Partition overlap in {pfile.name}: {name} [{hex(offset)}..{hex(end)}] "
                            f"vs {prev_n} [{hex(prev_s)}..{hex(prev_e)}]",
                        )
                    regions.append((offset, end, name))
                    cursor = end

    def test_lang_zh_cn_sorted_and_glyphs_present(self) -> None:
        lang_text = (REPO_ROOT / "src" / "core" / "lang_zh_cn.h").read_text(encoding="utf-8")
        cjk_text = (REPO_ROOT / "src" / "core" / "cjk_font_data.h").read_text(encoding="utf-8")

        pattern = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
        entries = []
        for m in pattern.finditer(lang_text):
            en = bytes(m.group(1), "utf-8").decode("unicode_escape")
            zh = m.group(2).replace(r"\n", "\n").replace(r"\"", '"')
            entries.append((en, zh))

        self.assertGreater(len(entries), 100)
        for i in range(1, len(entries)):
            self.assertLess(
                entries[i - 1][0].encode("utf-8"),
                entries[i][0].encode("utf-8"),
                f"kZhCnTable unsorted at index {i}: {entries[i-1][0]!r} >= {entries[i][0]!r}",
            )

        present_cps = {int(m, 16) for m in re.findall(r"\{0x([0-9A-Fa-f]{4}),", cjk_text)}
        missing: set[str] = set()
        for _, zh in entries:
            for ch in zh:
                if 0x80 <= ord(ch) <= 0xFFFF and ord(ch) not in present_cps:
                    missing.add(ch)
        self.assertEqual(missing, set(), f"Missing CJK glyphs in cjk_font_data.h: {sorted(missing)}")

    def test_keystroke_abi_preserved_in_globals_h(self) -> None:
        globals_h = (REPO_ROOT / "include" / "globals.h").read_text(encoding="utf-8")
        m = re.search(r"struct\s+keyStroke\s*\{([^}]+)\}", globals_h, re.S)
        self.assertIsNotNone(m, "struct keyStroke not found in include/globals.h")
        body = m.group(1)
        for required in ("pressed", "enter", "del", "word"):
            self.assertIn(required, body, f"struct keyStroke missing required field: {required}")

    def test_python_build_and_tool_scripts_compile(self) -> None:
        py_files = list((REPO_ROOT / "scripts" / "build").glob("*.py")) + list((REPO_ROOT / "tools").glob("*.py"))
        self.assertGreaterEqual(len(py_files), 2)
        for pf in py_files:
            with self.subTest(script=str(pf.relative_to(REPO_ROOT))):
                py_compile.compile(str(pf), doraise=True)

    def test_board_ini_definitions_have_env_sections(self) -> None:
        ini_files = sorted((REPO_ROOT / "boards").glob("*/*.ini"))
        self.assertGreaterEqual(len(ini_files), 4)
        for ini in ini_files:
            txt = ini.read_text(encoding="utf-8", errors="ignore")
            self.assertRegex(txt, r"\[env:[^\]]+\]", f"{ini.name} missing [env:...] section")


if __name__ == "__main__":
    unittest.main(verbosity=2)
