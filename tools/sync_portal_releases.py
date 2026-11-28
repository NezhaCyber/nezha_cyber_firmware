#!/usr/bin/env python3
"""Sync published GitHub Releases (Stable, Beta, Archive) into the static /portal manifest.

Used by .github/workflows/pages.yml (and optionally locally) so the Web Serial portal
displays every GitHub Release with real binary sizes, Flash/RAM usage, and same-origin
mirrored .bin images (avoiding GitHub Releases CORS restrictions in browsers).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

# Canonical board catalog matching .github/workflows/release.yml & partitions/*.csv
BOARD_CATALOG: dict[str, dict[str, Any]] = {
    "lilygo-t-deck": {
        "boardName": "LILYGO T-Deck",
        "boardNameZh": "LILYGO T-Deck 标准版",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB OPI PSRAM",
        "display": "320×240 ST7789 + GT911 Touch",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": True,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Power OFF -> Hold Trackball (GPIO0) -> Power ON -> Release Trackball.",
        "downloadModeHintZh": "关闭电源开关 -> 按住轨迹球 (GPIO0) -> 打开电源开关 -> 松开轨迹球。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "lilygo-t-deck-pro": {
        "boardName": "LILYGO T-Deck Plus",
        "boardNameZh": "LILYGO T-Deck Plus 增强版",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB OPI PSRAM",
        "display": "320×240 ST7789 + LoRa + GPS",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": True,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Power OFF -> Hold Trackball (GPIO0) -> Power ON -> Release Trackball.",
        "downloadModeHintZh": "关闭电源开关 -> 按住轨迹球 (GPIO0) -> 打开电源开关 -> 松开轨迹球。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "m5stack-cplus1_1": {
        "boardName": "M5StickC Plus 1.1",
        "boardNameZh": "M5StickC Plus 1.1 (4MB 紧凑版)",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "4 MB",
        "flashSizeMB": 4,
        "maxAppBytes": 0x3E8000,
        "edition": "compact",
        "psram": "None",
        "display": "240×135 ST7789 + 3 Buttons",
        "partitionTable": "partitions/custom_4Mb_full.csv",
        "featured": True,
        "usbHints": [
            {"vendorId": "0x0403", "productId": "0x6001"},
            {"vendorId": "0x1a86", "productId": "0x55d4"},
        ],
        "downloadModeHintEn": "Connect via USB-C data cable. Auto-reset via DTR/RTS is handled automatically.",
        "downloadModeHintZh": "直接通过 USB-C 数据线连接即可，DTR/RTS 将自动触发下载模式。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x3E8000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x3F8000", "size": "0x8000", "role": "fs"},
        ],
    },
    "LAUNCHER_m5stack-cplus1_1": {
        "boardName": "M5StickC Plus 1.1 (Launcher Lite)",
        "boardNameZh": "M5StickC Plus 1.1 (轻量启动器版 <4MB)",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "4 MB",
        "flashSizeMB": 4,
        "maxAppBytes": 0x270000,
        "edition": "lite",
        "psram": "None",
        "display": "240×135 ST7789 · Extra 1.5MB FS",
        "partitionTable": "partitions/custom_4Mb.csv",
        "featured": False,
        "usbHints": [
            {"vendorId": "0x0403", "productId": "0x6001"},
            {"vendorId": "0x1a86", "productId": "0x55d4"},
        ],
        "downloadModeHintEn": "Connect via USB-C data cable. Ideal for 4MB devices needing extra LittleFS storage or M5Launcher.",
        "downloadModeHintZh": "USB-C 直连刷入；适合需要更大板载空间或搭配 M5Launcher 使用的 4MB 设备。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x270000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x280000", "size": "0x180000", "role": "fs"},
        ],
    },
    "m5stack-cardputer": {
        "boardName": "M5Stack Cardputer",
        "boardNameZh": "M5Stack Cardputer 卡片电脑",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "8 MB",
        "flashSizeMB": 8,
        "maxAppBytes": 0x4E0000,
        "edition": "standard",
        "psram": "None (StampS3)",
        "display": "240×135 ST7789 + Keyboard",
        "partitionTable": "partitions/custom_8Mb.csv",
        "featured": True,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Power OFF -> Hold G0 button next to USB-C -> Plug USB-C or switch ON -> Release G0.",
        "downloadModeHintZh": "关闭电源 -> 按住 USB-C 旁的 G0 键 -> 插入 USB-C 或开启电源 -> 松开 G0。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x4E0000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x4F0000", "size": "0x300000", "role": "fs"},
            {"label": "CoreDump", "offset": "0x7F0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "lilygo-t-embed-cc1101": {
        "boardName": "LILYGO T-Embed CC1101",
        "boardNameZh": "LILYGO T-Embed CC1101",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB OPI PSRAM",
        "display": "320×170 ST7789 + Rotary Encoder",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Hold the center Encoder button (GPIO0) while plugging USB-C or pressing Reset.",
        "downloadModeHintZh": "插入 USB-C 或按下 Reset 键时，按住中心旋钮按键 (GPIO0)。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "lilygo-t-embed": {
        "boardName": "LILYGO T-Embed",
        "boardNameZh": "LILYGO T-Embed 标准版",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB OPI PSRAM",
        "display": "320×170 ST7789 + Rotary Encoder",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Hold the center Encoder button (GPIO0) while plugging USB-C or pressing Reset.",
        "downloadModeHintZh": "插入 USB-C 或按下 Reset 键时，按住中心旋钮按键 (GPIO0)。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "m5stack-cplus2": {
        "boardName": "M5StickC Plus2",
        "boardNameZh": "M5StickC Plus2 (8MB)",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "8 MB",
        "flashSizeMB": 8,
        "maxAppBytes": 0x4E0000,
        "edition": "standard",
        "psram": "2 MB PSRAM",
        "display": "240×135 ST7789 + 3 Buttons",
        "partitionTable": "partitions/custom_8Mb.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x1a86", "productId": "0x55d4"}],
        "downloadModeHintEn": "Connect via USB-C data cable. Auto-reset via DTR/RTS is handled automatically.",
        "downloadModeHintZh": "直接通过 USB-C 数据线连接即可，自动进入下载模式。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x4E0000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x4F0000", "size": "0x300000", "role": "fs"},
            {"label": "CoreDump", "offset": "0x7F0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "m5stack-cores3": {
        "boardName": "M5Stack CoreS3",
        "boardNameZh": "M5Stack CoreS3",
        "mcu": "ESP32-S3",
        "chipFamily": "esp32s3",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB PSRAM",
        "display": "320×240 IPS Touch",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x303a", "productId": "0x1001"}],
        "downloadModeHintEn": "Hold the Reset button on the side for ~3 seconds until the green LED lights up.",
        "downloadModeHintZh": "长按侧面 Reset 键约 3 秒直到绿灯亮起进入下载模式。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x0000", "size": "0x8000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "m5stack-core2": {
        "boardName": "M5Stack Core2",
        "boardNameZh": "M5Stack Core2",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "16 MB",
        "flashSizeMB": 16,
        "maxAppBytes": 0x640000,
        "edition": "full",
        "psram": "8 MB PSRAM",
        "display": "320×240 IPS Touch",
        "partitionTable": "partitions/custom_16Mb.csv",
        "featured": False,
        "usbHints": [
            {"vendorId": "0x10c4", "productId": "0xea60"},
            {"vendorId": "0x1a86", "productId": "0x55d4"},
        ],
        "downloadModeHintEn": "Connect via USB-C data cable. Auto-reset via CP2104/CH9102 is automatic.",
        "downloadModeHintZh": "直接通过 USB-C 数据线连接即可，板载串口芯片自动触发下载模式。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x640000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x650000", "size": "0x9A0000", "role": "fs"},
            {"label": "CoreDump", "offset": "0xFF0000", "size": "0x10000", "role": "dump"},
        ],
    },
    "CYD-2432S028": {
        "boardName": 'CYD-2432S028 (2.8")',
        "boardNameZh": "CYD-2432S028 2.8寸屏 (4MB 紧凑版)",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "4 MB",
        "flashSizeMB": 4,
        "maxAppBytes": 0x3E8000,
        "edition": "compact",
        "psram": "None",
        "display": "320×240 ILI9341 Touch",
        "partitionTable": "partitions/custom_4Mb_full.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x1a86", "productId": "0x7523"}],
        "downloadModeHintEn": "Hold BOOT while pressing Reset (or auto-reset via CH340 USB-UART).",
        "downloadModeHintZh": "按住 BOOT 键同时按 Reset（多数 CH340 版本支持自动下载）。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x3E8000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x3F8000", "size": "0x8000", "role": "fs"},
        ],
    },
    "LAUNCHER_CYD-2432S028": {
        "boardName": "CYD-2432S028 (Launcher Lite)",
        "boardNameZh": "CYD-2432S028 (轻量启动器版 <4MB)",
        "mcu": "ESP32",
        "chipFamily": "esp32",
        "flashSize": "4 MB",
        "flashSizeMB": 4,
        "maxAppBytes": 0x270000,
        "edition": "lite",
        "psram": "None",
        "display": "320×240 ILI9341 · Extra 1.5MB FS",
        "partitionTable": "partitions/custom_4Mb.csv",
        "featured": False,
        "usbHints": [{"vendorId": "0x1a86", "productId": "0x7523"}],
        "downloadModeHintEn": "Hold BOOT while pressing Reset. Compact <4MB Lite build for CYD.",
        "downloadModeHintZh": "按住 BOOT 键同时按 Reset；适合 4MB CYD 的轻量版本。",
        "partitions": [
            {"label": "Bootloader", "offset": "0x1000", "size": "0x7000", "role": "boot"},
            {"label": "Partition Table", "offset": "0x8000", "size": "0x1000", "role": "table"},
            {"label": "NVS", "offset": "0x9000", "size": "0x6000", "role": "nvs"},
            {"label": "Factory App", "offset": "0x10000", "size": "0x270000", "role": "app"},
            {"label": "LittleFS / SPIFFS", "offset": "0x280000", "size": "0x180000", "role": "fs"},
        ],
    },
}

BOARD_ORDER = list(BOARD_CATALOG.keys())


def gh_request_json(url: str, token: str | None) -> Any:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "NezhaCyber-Portal-Sync/1.0",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def download_file(url: str, dest: Path, token: str | None) -> int:
    headers = {"User-Agent": "NezhaCyber-Portal-Sync/1.0"}
    if token and "api.github.com" in url:
        headers["Authorization"] = f"Bearer {token}"
        headers["Accept"] = "application/octet-stream"
    req = urllib.request.Request(url, headers=headers)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=60) as resp, dest.open("wb") as out:
        total = 0
        while True:
            chunk = resp.read(65536)
            if not chunk:
                break
            out.write(chunk)
            total += len(chunk)
    return total


def build_portal_release(
    gh_rel: dict[str, Any],
    release_manifest: dict[str, Any] | None,
    mirrored_files: set[str],
    seen_first_stable: bool,
) -> dict[str, Any]:
    tag = str(gh_rel.get("tag_name") or "").strip()
    is_pre = bool(gh_rel.get("prerelease")) or ("-" in tag)
    if is_pre:
        channel = "beta"
    elif not seen_first_stable:
        channel = "stable"
    else:
        channel = "archive"

    published = str(gh_rel.get("published_at") or gh_rel.get("created_at") or "")[:10]
    if not published:
        published = datetime.now(timezone.utc).strftime("%Y-%m-%d")

    assets_by_name: dict[str, dict[str, Any]] = {
        str(a.get("name", "")): a for a in gh_rel.get("assets", []) if isinstance(a, dict)
    }
    meta_by_env: dict[str, dict[str, Any]] = {}
    if release_manifest and isinstance(release_manifest.get("boards"), list):
        for b in release_manifest["boards"]:
            if isinstance(b, dict) and b.get("env"):
                meta_by_env[str(b["env"])] = b

    builds: list[dict[str, Any]] = []
    for env_name in BOARD_ORDER:
        factory_name = f"NezhaCyber-{env_name}.bin"
        ota_name = f"NezhaCyber-{env_name}-ota.bin"
        asset = assets_by_name.get(factory_name)
        ota_asset = assets_by_name.get(ota_name)
        rmeta = meta_by_env.get(env_name, {})

        # Only include boards that actually exist in this GitHub Release (or all if release has no bins yet)
        if assets_by_name and not asset and env_name not in meta_by_env:
            continue

        tpl = BOARD_CATALOG[env_name]
        factory_size = int(rmeta.get("factory_size") or (asset.get("size") if asset else 0) or 0)
        ota_size = int(rmeta.get("ota_size") or (ota_asset.get("size") if ota_asset else 0) or 0)
        max_app = int(rmeta.get("app_partition_bytes") or tpl["maxAppBytes"])
        effective_app = ota_size if ota_size > 0 else factory_size
        usage_pct = round((effective_app / max_app) * 100.0, 1) if (max_app > 0 and effective_app > 0) else None

        static_ready = factory_name in mirrored_files
        static_path = f"firmware/releases/{tag}/{factory_name}"
        download_url = (
            str(asset.get("browser_download_url"))
            if asset and asset.get("browser_download_url")
            else str(rmeta.get("factory_url") or "")
        )

        build_entry: dict[str, Any] = {
            "id": env_name,
            "env": env_name,
            "boardName": tpl["boardName"],
            "boardNameZh": tpl["boardNameZh"],
            "mcu": tpl["mcu"],
            "chipFamily": tpl["chipFamily"],
            "flashSize": tpl["flashSize"],
            "flashSizeMB": tpl["flashSizeMB"],
            "maxAppBytes": max_app,
            "edition": rmeta.get("edition") or tpl["edition"],
            "psram": tpl["psram"],
            "display": tpl["display"],
            "partitionTable": tpl["partitionTable"],
            "featured": tpl["featured"],
            "usbHints": tpl["usbHints"],
            "downloadModeHintEn": tpl["downloadModeHintEn"],
            "downloadModeHintZh": tpl["downloadModeHintZh"],
            "partitions": tpl["partitions"],
            "binarySizeBytes": factory_size if factory_size > 0 else None,
            "otaSizeBytes": ota_size if ota_size > 0 else None,
            "flashUsagePct": usage_pct,
            "ramUsedBytes": int(rmeta.get("ram_used_bytes") or 0) or None,
            "ramTotalBytes": int(rmeta.get("ram_total_bytes") or 0) or None,
            "sha256": rmeta.get("factory_sha256"),
            "downloadUrl": download_url or None,
            "staticReady": static_ready,
            "flashModes": {
                "merged": {
                    "labelEn": "Full Merged Factory Image (0x0)",
                    "labelZh": "完整合并出厂镜像 (0x0)",
                    "parts": [
                        {
                            "name": factory_name,
                            "label": "Merged Factory Image",
                            "offset": "0x0",
                            "path": static_path,
                            "sizeBytes": factory_size if factory_size > 0 else None,
                            "downloadUrl": download_url or None,
                        }
                    ],
                }
            },
        }
        builds.append(build_entry)

    body_first_line = ""
    for line in str(gh_rel.get("body") or "").splitlines():
        s = line.strip().lstrip("#>*- ").strip()
        if s and not s.startswith("|") and not s.startswith("```"):
            body_first_line = s[:180]
            break

    return {
        "version": tag,
        "channel": channel,
        "releaseDate": published,
        "releaseUrl": gh_rel.get("html_url") or None,
        "notesEn": body_first_line or f"GitHub Release {tag} ({channel}).",
        "notesZh": body_first_line or f"GitHub 发行版 {tag}（{'测试版' if channel == 'beta' else '正式版'}）。",
        "builds": builds,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync GitHub Releases into static /portal dist/")
    parser.add_argument("--repo", default="NezhaCyber/nezha_cyber_firmware")
    parser.add_argument("--dist-dir", type=Path, required=True)
    parser.add_argument("--max-releases", type=int, default=12)
    parser.add_argument("--max-mirror-releases", type=int, default=4)
    parser.add_argument("--max-mirror-mb", type=int, default=350)
    parser.add_argument("--sync-bins", action="store_true")
    parser.add_argument("--token", default=os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
    args = parser.parse_args()

    api_url = f"https://api.github.com/repos/{args.repo}/releases?per_page={args.max_releases}"
    try:
        raw_releases = gh_request_json(api_url, args.token)
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        print(f"Notice: Could not query GitHub Releases ({exc}); keeping bundled portal manifest.")
        return 0

    if not isinstance(raw_releases, list):
        print("Notice: Unexpected GitHub Releases payload; keeping bundled portal manifest.")
        return 0

    published_rels = [r for r in raw_releases if isinstance(r, dict) and not r.get("draft")]
    if not published_rels:
        print("Notice: No published GitHub Releases found yet; keeping bundled portal manifest.")
        return 0

    # Pick which releases to mirror binaries for (guarantee latest stable + latest beta)
    mirror_tags: list[str] = []
    first_stable = next((r["tag_name"] for r in published_rels if not r.get("prerelease") and "-" not in str(r.get("tag_name", ""))), None)
    first_beta = next((r["tag_name"] for r in published_rels if r.get("prerelease") or "-" in str(r.get("tag_name", ""))), None)
    for t in (first_stable, first_beta):
        if t and t not in mirror_tags:
            mirror_tags.append(t)
    for r in published_rels:
        if len(mirror_tags) >= args.max_mirror_releases:
            break
        t = str(r.get("tag_name") or "")
        if t and t not in mirror_tags:
            mirror_tags.append(t)

    max_bytes = args.max_mirror_mb * 1024 * 1024
    mirrored_total = 0
    portal_releases: list[dict[str, Any]] = []
    seen_stable = False

    for gh_rel in published_rels:
        tag = str(gh_rel.get("tag_name") or "").strip()
        if not tag:
            continue
        assets = [a for a in gh_rel.get("assets", []) if isinstance(a, dict)]

        # Download release manifest.json if attached by release.yml for rich RAM/partition metadata
        rel_manifest: dict[str, Any] | None = None
        m_asset = next((a for a in assets if a.get("name") == "manifest.json"), None)
        if m_asset and m_asset.get("browser_download_url"):
            try:
                rel_manifest = gh_request_json(str(m_asset["browser_download_url"]), args.token)
            except Exception as exc:
                print(f"Notice: Could not fetch manifest.json for {tag}: {exc}")

        mirrored_names: set[str] = set()
        if args.sync_bins and tag in mirror_tags:
            rel_dir = args.dist_dir / "firmware" / "releases" / tag
            for a in assets:
                name = str(a.get("name") or "")
                if not (name.startswith("NezhaCyber-") and name.endswith(".bin")) or name.endswith("-ota.bin"):
                    continue
                size = int(a.get("size") or 0)
                if mirrored_total + size > max_bytes:
                    print(f"Cap reached ({args.max_mirror_mb}MB): skipping mirror of {tag}/{name}")
                    continue
                dl_url = str(a.get("browser_download_url") or "")
                if not dl_url:
                    continue
                try:
                    written = download_file(dl_url, rel_dir / name, args.token)
                    mirrored_total += written
                    mirrored_names.add(name)
                except Exception as exc:
                    print(f"Warning: Failed mirroring {tag}/{name}: {exc}")

        entry = build_portal_release(gh_rel, rel_manifest, mirrored_names, seen_stable)
        if entry["channel"] == "stable":
            seen_stable = True
        if entry["builds"]:
            portal_releases.append(entry)

    if not portal_releases:
        print("Notice: Published releases had no NezhaCyber-*.bin assets; keeping bundled portal manifest.")
        return 0

    manifest_path = args.dist_dir / "firmware" / "manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    out_manifest = {
        "project": "Nezha Cyber Firmware",
        "projectZh": "哪吒赛博固件",
        "repository": args.repo,
        "schemaVersion": 3,
        "updatedAt": datetime.now(timezone.utc).strftime("%Y-%m-%d"),
        "releases": portal_releases,
    }
    manifest_path.write_text(
        json.dumps(out_manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"Synced {len(portal_releases)} GitHub Releases into {manifest_path} "
        f"(mirrored {mirrored_total / (1024 * 1024):.1f} MB same-origin binaries)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
