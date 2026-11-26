#!/usr/bin/env python3
"""Generate and verify compact 1-bit PROGMEM Unicode bitmap font tables for Nezha Cyber.

- Core table (`kCjkGlyphs[]`, all boards including 4MB compact):
  Covers `src/core/lang_zh_cn.h` and core Simplified Chinese UI labels.
- Extended table (`kExtGlyphs[]`, 8MB/16MB boards when `!BRUCE_COMPACT_CJK_FONT`):
  Covers Korean (Hangul), Japanese (Kana/Kanji), Russian (Cyrillic), German, Spanish & French
  from `src/core/i18n/lang_locales.h` and native language names in `src/core/settings.cc`.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
LANG_HEADER = REPO_ROOT / "src" / "core" / "lang_zh_cn.h"
LOCALES_HEADER = REPO_ROOT / "src" / "core" / "i18n" / "lang_locales.h"
OUT_HEADER = REPO_ROOT / "src" / "core" / "cjk_font_data.h"

CORE_EXTRA_SCAN_FILES = [
    REPO_ROOT / "src" / "core" / "menu_items" / "ConfigMenu.cc",
]
EXT_EXTRA_SCAN_FILES = [
    REPO_ROOT / "src" / "core" / "settings.cc",
    REPO_ROOT / "src" / "core" / "i18n.cc",
]

CJK_FONT_CANDIDATES = [
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
]

KOREAN_FONT_CANDIDATES = [
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansKR-Regular.ttf",
]

LATIN_CYRILLIC_FONT_CANDIDATES = [
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/SFNS.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
]

EXTRA_CHARS = "简体中文语言选择返回确认取消开关是否成功失败错误警告加载中请稍候"
EXT_EXTRA_CHARS = "한국어日本語DeutschРусскийEspañolFrançais"


def find_font(candidates: list[str], env_key: str | None = None) -> str:
    if env_key:
        env_font = os.environ.get(env_key)
        if env_font and Path(env_font).is_file():
            return env_font
    for path in candidates:
        if Path(path).is_file():
            return path
    raise RuntimeError(f"No suitable font found among: {candidates}")


def parse_entries_in_block(block_text: str, label: str) -> list[tuple[str, str]]:
    pattern = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
    entries: list[tuple[str, str]] = []
    for m in pattern.finditer(block_text):
        en = bytes(m.group(1), "utf-8").decode("unicode_escape")
        tr = m.group(2).replace(r"\n", "\n").replace(r"\"", '"')
        entries.append((en, tr))

    if not entries:
        raise ValueError(f"No translation entries found in {label}")

    for i in range(1, len(entries)):
        prev_en = entries[i - 1][0].encode("utf-8")
        curr_en = entries[i][0].encode("utf-8")
        if prev_en >= curr_en:
            raise ValueError(
                f"{label} not strictly sorted at index {i}: "
                f"{entries[i - 1][0]!r} >= {entries[i][0]!r}"
            )
    return entries


def parse_locale_tables(path: Path) -> dict[str, list[tuple[str, str]]]:
    text = path.read_text(encoding="utf-8")
    tables: dict[str, list[tuple[str, str]]] = {}
    block_pat = re.compile(
        r"static\s+const\s+TranslationEntry\s+(k[A-Za-z0-9_]+)\[\]\s+PROGMEM\s*=\s*\{(.*?)\};",
        re.S,
    )
    for m in block_pat.finditer(text):
        name = m.group(1)
        tables[name] = parse_entries_in_block(m.group(2), f"{path.name}:{name}")
    if not tables:
        raise ValueError(f"No TranslationEntry tables found in {path}")
    return tables


def collect_codepoints_from_entries(
    entries: Iterable[tuple[str, str]], extra_chars: str, extra_files: list[Path]
) -> set[int]:
    cps: set[int] = set()
    for _, tr in entries:
        for ch in tr:
            if 0x80 <= ord(ch) <= 0xFFFF:
                cps.add(ord(ch))
    for ch in extra_chars:
        if 0x80 <= ord(ch) <= 0xFFFF:
            cps.add(ord(ch))
    for p in extra_files:
        if p.is_file():
            for ch in p.read_text(encoding="utf-8", errors="ignore"):
                if 0x80 <= ord(ch) <= 0xFFFF:
                    cps.add(ord(ch))
    return cps


def is_hangul(cp: int) -> bool:
    return (0x1100 <= cp <= 0x11FF) or (0x3130 <= cp <= 0x318F) or (0xAC00 <= cp <= 0xD7AF)


def is_latin_or_cyrillic(cp: int) -> bool:
    return 0x0080 <= cp <= 0x052F


def rasterize_glyph(ch: str, font: ImageFont.FreeTypeFont, size: int) -> list[int]:
    img = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (size - w) // 2 - bbox[0]
    y = (size - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=255)

    thresh = 96 if size <= 12 else 112
    rows: list[int] = []
    px = img.load()
    for ry in range(size):
        bits = 0
        for rx in range(size):
            if px[rx, ry] >= thresh:
                bits |= 1 << (size - 1 - rx)
        rows.append(bits)
    return rows


def generate_header(core_cps: list[int], ext_cps: list[int]) -> str:
    cjk_path = find_font(CJK_FONT_CANDIDATES, "BRUCE_CJK_FONT")
    kr_path = find_font(KOREAN_FONT_CANDIDATES, "NEZHA_KR_FONT") if ext_cps else cjk_path
    lat_path = find_font(LATIN_CYRILLIC_FONT_CANDIDATES, "NEZHA_LATIN_FONT") if ext_cps else cjk_path

    fonts12 = {
        "cjk": ImageFont.truetype(cjk_path, 11),
        "kr": ImageFont.truetype(kr_path, 11),
        "lat": ImageFont.truetype(lat_path, 11),
    }
    fonts16 = {
        "cjk": ImageFont.truetype(cjk_path, 14),
        "kr": ImageFont.truetype(kr_path, 14),
        "lat": ImageFont.truetype(lat_path, 14),
    }

    def pick_key(cp: int) -> str:
        if is_hangul(cp):
            return "kr"
        if is_latin_or_cyrillic(cp):
            return "lat"
        return "cjk"

    lines: list[str] = [
        "// AUTOGENERATED by tools/gen_cjk_font.py - DO NOT EDIT BY HAND",
        "#ifndef __BRUCE_CJK_FONT_DATA_H__",
        "#define __BRUCE_CJK_FONT_DATA_H__",
        "",
        "#include <Arduino.h>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace bruce_i18n {",
        "",
        "struct CjkGlyph {",
        "    uint16_t codepoint;",
        "    uint16_t rows12[12]; // 12x12 bitmap (MSB = leftmost pixel in bits 11..0)",
        "#if !defined(BRUCE_COMPACT_CJK_FONT)",
        "    uint16_t rows16[16]; // 16x16 bitmap (MSB = leftmost pixel in bits 15..0)",
        "#endif",
        "};",
        "",
        "#if defined(BRUCE_COMPACT_CJK_FONT)",
        "#define BRUCE_CJK_R16(...)",
        "#else",
        "#define BRUCE_CJK_R16(...) , {__VA_ARGS__}",
        "#endif",
        "",
        f"static constexpr size_t kCjkGlyphCount = {len(core_cps)};",
        "",
        "static const CjkGlyph kCjkGlyphs[kCjkGlyphCount] PROGMEM = {",
    ]

    for cp in core_cps:
        ch = chr(cp)
        fk = pick_key(cp)
        r12 = rasterize_glyph(ch, fonts12[fk], 12)
        r16 = rasterize_glyph(ch, fonts16[fk], 16)
        r12_str = ", ".join(f"0x{v:03X}" for v in r12)
        r16_str = ", ".join(f"0x{v:04X}" for v in r16)
        safe_comment = ch if ch not in ("\\", "*/") else ""
        lines.append(f"    {{0x{cp:04X}, {{{r12_str}}} BRUCE_CJK_R16({r16_str})}}, // {safe_comment}")

    lines += [
        "};",
        "",
        "#if !defined(BRUCE_COMPACT_CJK_FONT)",
        f"static constexpr size_t kExtGlyphCount = {len(ext_cps)};",
        "",
        "static const CjkGlyph kExtGlyphs[kExtGlyphCount] PROGMEM = {",
    ]

    for cp in ext_cps:
        ch = chr(cp)
        fk = pick_key(cp)
        r12 = rasterize_glyph(ch, fonts12[fk], 12)
        r16 = rasterize_glyph(ch, fonts16[fk], 16)
        r12_str = ", ".join(f"0x{v:03X}" for v in r12)
        r16_str = ", ".join(f"0x{v:04X}" for v in r16)
        safe_comment = ch if ch not in ("\\", "*/") else ""
        lines.append(f"    {{0x{cp:04X}, {{{r12_str}}} BRUCE_CJK_R16({r16_str})}}, // {safe_comment}")

    lines += [
        "};",
        "#endif // !defined(BRUCE_COMPACT_CJK_FONT)",
        "#undef BRUCE_CJK_R16",
        "",
        "} // namespace bruce_i18n",
        "",
        "#endif // __BRUCE_CJK_FONT_DATA_H__",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Nezha Cyber Unicode/CJK bitmap font header")
    parser.add_argument("--check", action="store_true", help="Verify table sort order and pixel-exact header freshness")
    parser.add_argument(
        "--check-tables",
        action="store_true",
        help="OS-independent CI check: verify strict sort order and that every required Unicode glyph exists in header",
    )
    args = parser.parse_args()

    zh_tables = parse_locale_tables(LANG_HEADER)
    zh_entries = zh_tables["kZhCnTable"]
    core_set = collect_codepoints_from_entries(zh_entries, EXTRA_CHARS, CORE_EXTRA_SCAN_FILES)

    ext_entries: list[tuple[str, str]] = []
    locale_counts = {"zh-CN": len(zh_entries)}
    if LOCALES_HEADER.is_file():
        loc_tables = parse_locale_tables(LOCALES_HEADER)
        for name, ents in loc_tables.items():
            ext_entries.extend(ents)
            locale_counts[name] = len(ents)

    ext_set = collect_codepoints_from_entries(ext_entries, EXT_EXTRA_CHARS, EXT_EXTRA_SCAN_FILES)
    ext_only = sorted(ext_set - core_set)
    core_sorted = sorted(core_set)

    if args.check_tables:
        if not OUT_HEADER.is_file():
            print(f"ERROR: {OUT_HEADER} does not exist.", file=sys.stderr)
            return 1
        existing = OUT_HEADER.read_text(encoding="utf-8")
        present_cps = {int(m, 16) for m in re.findall(r"\{0x([0-9A-Fa-f]{4}),", existing)}
        missing_core = sorted(core_set - present_cps)
        missing_ext = sorted(set(ext_only) - present_cps)
        if missing_core or missing_ext:
            print(
                f"ERROR: Missing glyphs in {OUT_HEADER.name}: "
                f"core={len(missing_core)}, ext={len(missing_ext)}. Run python3 tools/gen_cjk_font.py.",
                file=sys.stderr,
            )
            return 1
        print(
            f"OK (CI): {len(locale_counts)} locale tables strictly sorted ({locale_counts}), "
            f"all {len(core_sorted)} core + {len(ext_only)} extended Unicode glyphs present."
        )
        return 0

    header = generate_header(core_sorted, ext_only)

    if args.check:
        if not OUT_HEADER.is_file():
            print(f"ERROR: {OUT_HEADER} does not exist. Run without --check.", file=sys.stderr)
            return 1
        existing = OUT_HEADER.read_text(encoding="utf-8")
        if existing != header:
            print(f"ERROR: {OUT_HEADER} is out of date. Run tools/gen_cjk_font.py.", file=sys.stderr)
            return 1
        print(
            f"OK: {len(locale_counts)} locale tables strictly sorted ({locale_counts}), "
            f"{len(core_sorted)} core CJK glyphs + {len(ext_only)} extended Unicode glyphs."
        )
        return 0

    OUT_HEADER.write_text(header, encoding="utf-8")
    print(
        f"Generated {OUT_HEADER.relative_to(REPO_ROOT)}: "
        f"{len(locale_counts)} locale tables ({locale_counts}), "
        f"{len(core_sorted)} core CJK + {len(ext_only)} extended Unicode glyphs."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
