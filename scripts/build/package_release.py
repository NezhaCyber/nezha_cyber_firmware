#!/usr/bin/env python3
"""Package Nezha Cyber GitHub Release artifacts: manifest.json, SHA256SUMS.txt, and RELEASE_NOTES.md."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Nezha Cyber GitHub Release manifest and notes")
    parser.add_argument("--assets-dir", type=Path, default=Path("release-assets"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--notes-out", type=Path, default=Path("RELEASE_NOTES.md"))
    args = parser.parse_args()

    out_dir: Path = args.assets_dir
    if not out_dir.is_dir():
        print(f"ERROR: assets dir not found: {out_dir}", file=sys.stderr)
        return 1

    metas: list[dict] = []
    for mf in sorted(out_dir.glob("meta-*.json")):
        data = json.loads(mf.read_text(encoding="utf-8"))
        fbin = out_dir / data["factory_bin"]
        obin = out_dir / data["ota_bin"]
        if fbin.is_file():
            data["factory_size"] = fbin.stat().st_size
            data["factory_sha256"] = sha256_file(fbin)
        if obin.is_file():
            data["ota_size"] = obin.stat().st_size
            data["ota_sha256"] = sha256_file(obin)
        metas.append(data)
        mf.unlink()

    manifest = {
        "project": "Nezha Cyber (哪吒网络安全)",
        "author": "钟智强 (ctkqiang)",
        "version": args.tag,
        "boards": metas,
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    sum_lines = [
        f"{sha256_file(p)}  {p.name}"
        for p in sorted(out_dir.iterdir())
        if p.is_file() and p.suffix in (".bin", ".zip")
    ]
    (out_dir / "SHA256SUMS.txt").write_text("\n".join(sum_lines) + "\n", encoding="utf-8")

    rows = []
    for m in metas:
        f_kb = f"{m.get('factory_size', 0) // 1024:,} KB" if m.get("factory_size") else "—"
        o_kb = f"{m.get('ota_size', 0) // 1024:,} KB" if m.get("ota_size") else "—"
        rows.append(
            f"| **{m['board']}** | `{m['mcu']}` ({m['flash']}) "
            f"| `{m['factory_bin']}` ({f_kb}) | `{m['ota_bin']}` ({o_kb}) |"
        )

    table_body = "\n".join(rows) if rows else "| *(no boards built)* | — | — | — |"
    notes = (
        f"# 🔥 Nezha Cyber (哪吒网络安全) `{args.tag}`\n\n"
        f"**Author / 作者:** 钟智强 ([ctkqiang](https://github.com/ctkqiang))  \n"
        f"**Standalone Fork Lineage:** Independently evolved from Bruce Firmware "
        f"(full upstream Git history preserved under AGPL-3.0).\n\n"
        f"---\n\n"
        f"## 📦 Firmware Downloads / 固件下载列表\n\n"
        f"| Target Board / 目标板卡 | MCU / Flash | Full Factory Image (`0x00000`) | OTA / M5Launcher App Image |\n"
        f"| :--- | :---: | :--- | :--- |\n"
        f"{table_body}\n\n"
        f"- **`NezhaCyber-sd-files.zip`**: Optional microSD card resources (JS scripts, IR/RF samples, themes).\n"
        f"- **`manifest.json`** & **`SHA256SUMS.txt`**: Release manifest and SHA-256 verification checksums.\n\n"
        f"## ⚡ Quick Flash Command / 快速烧录命令\n\n"
        f"```sh\n"
        f"# LilyGO T-Deck (ESP32-S3) — full merged image at 0x00000\n"
        f"esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x00000 NezhaCyber-lilygo-t-deck.bin\n\n"
        f"# M5StickC Plus 1.1 (ESP32) — full merged image at 0x00000\n"
        f"esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash 0x00000 NezhaCyber-m5stack-cplus1_1.bin\n"
        f"```\n\n"
        f"---\n"
    )
    args.notes_out.write_text(notes, encoding="utf-8")
    print(f"Packaged release {args.tag}: {len(metas)} boards, {len(sum_lines)} checksummed files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
