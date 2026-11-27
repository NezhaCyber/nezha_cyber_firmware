#!/usr/bin/env python3
"""Generate esp-web-tools WebSerial flasher manifests for Nezha Cyber releases."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

BOARDS = [
    {"env": "lilygo-t-deck",             "name": "Nezha Cyber — LilyGO T-Deck",               "chip": "ESP32-S3", "flash": "16MB"},
    {"env": "lilygo-t-deck-pro",         "name": "Nezha Cyber — LilyGO T-Deck Plus",          "chip": "ESP32-S3", "flash": "16MB"},
    {"env": "m5stack-cardputer",         "name": "Nezha Cyber — M5Stack Cardputer",           "chip": "ESP32-S3", "flash": "8MB"},
    {"env": "lilygo-t-embed-cc1101",     "name": "Nezha Cyber — LilyGO T-Embed CC1101",       "chip": "ESP32-S3", "flash": "16MB"},
    {"env": "lilygo-t-embed",            "name": "Nezha Cyber — LilyGO T-Embed",              "chip": "ESP32-S3", "flash": "16MB"},
    {"env": "m5stack-cplus1_1",          "name": "Nezha Cyber — M5StickC Plus 1.1",           "chip": "ESP32",    "flash": "4MB"},
    {"env": "LAUNCHER_m5stack-cplus1_1", "name": "Nezha Cyber — M5StickC Plus 1.1 (Launcher)","chip": "ESP32",    "flash": "4MB"},
    {"env": "m5stack-cplus2",            "name": "Nezha Cyber — M5StickC Plus2",              "chip": "ESP32",    "flash": "8MB"},
    {"env": "m5stack-cores3",            "name": "Nezha Cyber — M5Stack CoreS3",              "chip": "ESP32-S3", "flash": "16MB"},
    {"env": "m5stack-core2",             "name": "Nezha Cyber — M5Stack Core2",               "chip": "ESP32",    "flash": "16MB"},
    {"env": "CYD-2432S028",              "name": "Nezha Cyber — CYD-2432S028 (2.8\")",        "chip": "ESP32",    "flash": "4MB"},
    {"env": "LAUNCHER_CYD-2432S028",     "name": "Nezha Cyber — CYD-2432S028 (Launcher)",     "chip": "ESP32",    "flash": "4MB"},
]


def build_manifest(board: dict[str, str], repo: str, tag: str, base_url: str | None) -> dict:
    bin_name = f"NezhaCyber-{board['env']}.bin"
    if base_url:
        bin_url = f"{base_url.rstrip('/')}/{bin_name}"
    elif tag == "latest":
        bin_url = f"https://github.com/{repo}/releases/latest/download/{bin_name}"
    else:
        bin_url = f"https://github.com/{repo}/releases/download/{tag}/{bin_name}"

    return {
        "name": board["name"],
        "version": tag,
        "home_assistant_domain": "nezha_cyber",
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": board["chip"],
                "parts": [
                    {
                        "path": bin_url,
                        "offset": 0,
                    }
                ],
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate esp-web-tools manifests for Nezha Cyber")
    parser.add_argument("--out-dir", type=Path, required=True, help="Output directory for manifest JSON files")
    parser.add_argument("--repo", default="ctkqiang/nezha_cyber_firmware", help="GitHub owner/repo")
    parser.add_argument("--tag", default="latest", help="Release tag (e.g. v2.1.0) or 'latest'")
    parser.add_argument("--base-url", default=None, help="Optional override base URL for .bin files")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    index: list[dict[str, str]] = []

    for board in BOARDS:
        manifest = build_manifest(board, args.repo, args.tag, args.base_url)
        out_path = args.out_dir / f"{board['env']}.json"
        out_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        index.append(
            {
                "env": board["env"],
                "name": board["name"],
                "chip": board["chip"],
                "flash": board["flash"],
                "manifest": f"manifests/{board['env']}.json",
            }
        )

    (args.out_dir / "boards.json").write_text(
        json.dumps({"version": args.tag, "repo": args.repo, "boards": index}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"Generated {len(BOARDS)} esp-web-tools manifests in {args.out_dir} (tag={args.tag})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
