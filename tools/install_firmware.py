#!/usr/bin/env python3
"""Nezha Cyber (哪吒网络安全) Smart Firmware Installer & GitHub Release Manager.

Features:
  - Queries GitHub Releases (Stable, Beta/Pre-release, Archive) with exact binary memory sizes
  - Probes connected ESP chip family & real Flash capacity (4MB / 8MB / 16MB) via esptool
  - Enforces Flash & App partition limits (e.g., strictly < 4MB for M5StickC Plus 1.1 / CYD)
  - Verifies SHA-256 checksums against SHA256SUMS.txt before flashing at 0x00000
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

DEFAULT_REPO = "NezhaCyber/nezha_cyber_firmware"


@dataclass(frozen=True)
class BoardSpec:
    env: str
    name: str
    mcu: str
    flash_mb: int
    app_partition_bytes: int
    edition: str  # full | standard | compact | lite

    @property
    def flash_bytes(self) -> int:
        return self.flash_mb * 1024 * 1024


BOARD_SPECS: dict[str, BoardSpec] = {
    "lilygo-t-deck": BoardSpec("lilygo-t-deck", "LILYGO T-Deck", "esp32s3", 16, 0x640000, "full"),
    "lilygo-t-deck-pro": BoardSpec("lilygo-t-deck-pro", "LILYGO T-Deck Plus", "esp32s3", 16, 0x640000, "full"),
    "lilygo-t-embed-cc1101": BoardSpec("lilygo-t-embed-cc1101", "LILYGO T-Embed CC1101", "esp32s3", 16, 0x640000, "full"),
    "lilygo-t-embed": BoardSpec("lilygo-t-embed", "LILYGO T-Embed", "esp32s3", 16, 0x640000, "full"),
    "m5stack-cardputer": BoardSpec("m5stack-cardputer", "M5Stack Cardputer", "esp32s3", 8, 0x4E0000, "standard"),
    "m5stack-cores3": BoardSpec("m5stack-cores3", "M5Stack CoreS3", "esp32s3", 16, 0x640000, "full"),
    "m5stack-core2": BoardSpec("m5stack-core2", "M5Stack Core2", "esp32", 16, 0x640000, "full"),
    "m5stack-cplus2": BoardSpec("m5stack-cplus2", "M5StickC Plus2", "esp32", 8, 0x4E0000, "standard"),
    "m5stack-cplus1_1": BoardSpec("m5stack-cplus1_1", "M5StickC Plus 1.1", "esp32", 4, 0x3E8000, "compact"),
    "LAUNCHER_m5stack-cplus1_1": BoardSpec("LAUNCHER_m5stack-cplus1_1", "M5StickC Plus 1.1 (Launcher Lite)", "esp32", 4, 0x270000, "lite"),
    "CYD-2432S028": BoardSpec("CYD-2432S028", 'CYD-2432S028 (2.8")', "esp32", 4, 0x3E8000, "compact"),
    "LAUNCHER_CYD-2432S028": BoardSpec("LAUNCHER_CYD-2432S028", "CYD-2432S028 (Launcher Lite)", "esp32", 4, 0x270000, "lite"),
}


def fmt_size(num_bytes: int) -> str:
    if num_bytes <= 0:
        return "--"
    if num_bytes >= 1024 * 1024:
        return f"{num_bytes / (1024 * 1024):.2f} MB"
    return f"{num_bytes / 1024:.0f} KB"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def find_esptool_cmd() -> list[str] | None:
    for candidate in ("esptool", "esptool.py"):
        found = shutil.which(candidate)
        if found:
            return [found]
    pio_venv = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    pio_bin = str(pio_venv) if pio_venv.is_file() else shutil.which("pio")
    if pio_bin:
        return [pio_bin, "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py"]
    return None


def discover_serial_ports() -> list[str]:
    patterns = (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.wchusbserial*",
        "/dev/ttyACM*",
        "/dev/ttyUSB*",
    )
    ports: list[str] = []
    for pat in patterns:
        for match in sorted(glob.glob(pat)):
            if match not in ports:
                ports.append(match)
    return ports


def parse_esptool_probe(output: str) -> tuple[str | None, int | None]:
    """Parse MCU family and detected Flash size (MB) from esptool flash-id output."""
    chip: str | None = None
    m_chip = re.search(r"Detecting chip type\.\.\.\s*([^\r\n]+)", output, re.IGNORECASE)
    if not m_chip:
        m_chip = re.search(r"Chip is\s+([A-Za-z0-9_-]+)", output, re.IGNORECASE)
    if m_chip:
        raw = re.sub(r"[^a-z0-9]", "", m_chip.group(1).lower())
        if "esp32s3" in raw:
            chip = "esp32s3"
        elif "esp32c5" in raw:
            chip = "esp32c5"
        elif "esp32c6" in raw:
            chip = "esp32c6"
        elif "esp32s2" in raw:
            chip = "esp32s2"
        elif "esp32" in raw:
            chip = "esp32"

    flash_mb: int | None = None
    m_flash = re.search(r"Detected flash size:\s*(\d+)\s*MB", output, re.IGNORECASE)
    if m_flash:
        flash_mb = int(m_flash.group(1))
    return chip, flash_mb


def probe_device(port: str) -> tuple[str | None, int | None, str]:
    esptool = find_esptool_cmd()
    if not esptool:
        return None, None, "esptool not found"
    proc = subprocess.run(
        [*esptool, "--port", port, "flash-id"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    chip, flash_mb = parse_esptool_probe(proc.stdout)
    return chip, flash_mb, proc.stdout


def recommend_envs(chip: str | None, flash_mb: int | None) -> list[BoardSpec]:
    out: list[BoardSpec] = []
    for spec in BOARD_SPECS.values():
        if chip and spec.mcu != chip:
            continue
        if flash_mb and spec.flash_mb > flash_mb:
            continue
        out.append(spec)
    return out


def validate_binary_fit(
    bin_path: Path,
    env_name: str,
    ota_path: Path | None = None,
    detected_flash_mb: int | None = None,
    detected_chip: str | None = None,
) -> tuple[bool, str]:
    if not bin_path.is_file():
        return False, f"Binary not found: {bin_path}"

    spec = BOARD_SPECS.get(env_name)
    bin_size = bin_path.stat().st_size
    if bin_size < 64:
        return False, f"Binary looks empty/corrupt ({bin_size} bytes): {bin_path}"

    if spec and detected_chip and spec.mcu != detected_chip:
        return (
            False,
            f"MCU mismatch: {env_name} targets {spec.mcu}, but connected chip is {detected_chip}.",
        )

    effective_flash_mb = detected_flash_mb or (spec.flash_mb if spec else None)
    if effective_flash_mb:
        limit_bytes = effective_flash_mb * 1024 * 1024
        if spec and spec.flash_mb > effective_flash_mb:
            hint = " Use a <4MB build (e.g. ENV=m5stack-cplus1_1 or ENV=LAUNCHER_m5stack-cplus1_1)." if effective_flash_mb <= 4 else ""
            return (
                False,
                f"Flash capacity mismatch: {env_name} requires {spec.flash_mb} MB flash, "
                f"but connected device only has {effective_flash_mb} MB.{hint}",
            )
        if bin_size > limit_bytes:
            hint = " Try Launcher Lite (ENV=LAUNCHER_m5stack-cplus1_1)." if effective_flash_mb <= 4 else ""
            return (
                False,
                f"Binary overflow: {bin_path.name} is {fmt_size(bin_size)} ({bin_size:,} B), "
                f"exceeding {effective_flash_mb} MB flash limit ({limit_bytes:,} B).{hint}",
            )

    if spec and ota_path and ota_path.is_file():
        ota_size = ota_path.stat().st_size
        if ota_size > spec.app_partition_bytes:
            return (
                False,
                f"App partition overflow: {ota_path.name} is {fmt_size(ota_size)} ({ota_size:,} B) "
                f"> {fmt_size(spec.app_partition_bytes)} ({spec.app_partition_bytes:,} B).",
            )
        pct = (ota_size / spec.app_partition_bytes) * 100.0
        return (
            True,
            f"OK: {bin_path.name} ({fmt_size(bin_size)}) | App {fmt_size(ota_size)} / "
            f"{fmt_size(spec.app_partition_bytes)} ({pct:.1f}%) fits {spec.flash_mb} MB {spec.mcu} [{spec.edition}]",
        )

    if spec:
        pct = (bin_size / spec.flash_bytes) * 100.0
        return (
            True,
            f"OK: {bin_path.name} ({fmt_size(bin_size)} / {spec.flash_mb}.00 MB · {pct:.1f}%) "
            f"fits {spec.name} [{spec.edition}]",
        )
    return True, f"OK: {bin_path.name} ({fmt_size(bin_size)})"


def gh_fetch_json(url: str, token: str | None = None) -> Any:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "NezhaCyber-CLI-Installer/1.0",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def gh_fetch_text(url: str, token: str | None = None) -> str:
    headers = {"User-Agent": "NezhaCyber-CLI-Installer/1.0"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return resp.read().decode("utf-8", errors="replace")


def gh_download_file(url: str, dest: Path, token: str | None = None) -> int:
    headers = {"User-Agent": "NezhaCyber-CLI-Installer/1.0"}
    if token and "api.github.com" in url:
        headers["Authorization"] = f"Bearer {token}"
        headers["Accept"] = "application/octet-stream"
    req = urllib.request.Request(url, headers=headers)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=90) as resp, dest.open("wb") as out:
        total = 0
        while True:
            chunk = resp.read(65536)
            if not chunk:
                break
            out.write(chunk)
            total += len(chunk)
    return total


def fetch_releases(repo: str, token: str | None = None, limit: int = 15) -> list[dict[str, Any]]:
    data = gh_fetch_json(f"https://api.github.com/repos/{repo}/releases?per_page={limit}", token)
    if not isinstance(data, list):
        return []
    return [r for r in data if isinstance(r, dict) and not r.get("draft")]


def release_channel(rel: dict[str, Any], is_first_stable: bool) -> str:
    tag = str(rel.get("tag_name") or "")
    if rel.get("prerelease") or "-" in tag:
        return "beta"
    return "stable" if is_first_stable else "archive"


def select_release(
    releases: list[dict[str, Any]], tag: str | None, channel: str | None
) -> dict[str, Any] | None:
    if not releases:
        return None
    if tag and tag not in ("latest", "stable", "beta"):
        for r in releases:
            if str(r.get("tag_name")) == tag:
                return r
        return None
    want_beta = (channel == "beta") or (tag == "beta")
    for r in releases:
        t = str(r.get("tag_name") or "")
        is_beta = bool(r.get("prerelease")) or ("-" in t)
        if want_beta and is_beta:
            return r
        if not want_beta and not is_beta:
            return r
    return releases[0]


def cmd_list_releases(args: argparse.Namespace) -> int:
    try:
        releases = fetch_releases(args.repo, args.token)
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        print(f"ERROR: Could not fetch GitHub Releases from {args.repo}: {exc}", file=sys.stderr)
        return 1

    if not releases:
        print(f"No published GitHub Releases found for {args.repo}.")
        return 0

    print(f"\n🔥 Nezha Cyber GitHub Releases ({args.repo})\n")
    seen_stable = False
    shown = 0
    for rel in releases:
        tag = str(rel.get("tag_name") or "")
        is_beta = bool(rel.get("prerelease")) or ("-" in tag)
        chan = release_channel(rel, not seen_stable and not is_beta)
        if chan == "stable":
            seen_stable = True
        if args.channel == "beta" and chan != "beta":
            continue
        if args.channel == "stable" and chan == "beta":
            continue

        pub = str(rel.get("published_at") or "")[:10]
        print(f"  {tag:<18} [{chan.upper():<7}]  {pub}  {rel.get('html_url', '')}")
        assets = {str(a.get("name")): a for a in rel.get("assets", []) if isinstance(a, dict)}
        for env_name, spec in BOARD_SPECS.items():
            if args.env and args.env != env_name:
                continue
            fname = f"NezhaCyber-{env_name}.bin"
            asset = assets.get(fname)
            if not asset:
                continue
            sz = int(asset.get("size") or 0)
            pct = (sz / spec.flash_bytes) * 100.0 if spec.flash_bytes else 0.0
            fit_tag = "<4MB OK" if (spec.flash_mb <= 4 and sz <= spec.flash_bytes) else f"{spec.flash_mb}MB"
            print(
                f"      - {env_name:<26} {spec.mcu:<7} {fit_tag:<8} "
                f"{fmt_size(sz):>8} / {spec.flash_mb:>2} MB ({pct:4.1f}%)  [{spec.edition}]"
            )
        shown += 1

    if shown == 0:
        print(f"No releases matched channel='{args.channel}'.")
    print("\nInstall example:")
    print("  make install RELEASE=latest ENV=lilygo-t-deck")
    print("  make install CHANNEL=beta   ENV=m5stack-cplus1_1\n")
    return 0


def cmd_check_bin(args: argparse.Namespace) -> int:
    ok, msg = validate_binary_fit(
        Path(args.check_bin),
        args.env,
        Path(args.ota_bin) if args.ota_bin else None,
        args.detected_flash_mb,
        args.detected_chip,
    )
    print(("✅ " if ok else "❌ ") + msg)
    return 0 if ok else 1


def cmd_probe(args: argparse.Namespace) -> int:
    port = args.probe_port
    chip, flash_mb, raw = probe_device(port)
    print(raw.strip())
    if chip or flash_mb:
        print(f"\nDetected: MCU={chip or 'unknown'} | Flash={f'{flash_mb} MB' if flash_mb else 'unknown'}")
        recs = recommend_envs(chip, flash_mb)
        if recs:
            print("Compatible firmware environments:")
            for r in recs:
                print(f"  - {r.env:<26} ({r.name} · {r.flash_mb} MB · {r.edition})")
        return 0
    print("Could not determine chip/flash size (ensure device is in Download Mode).", file=sys.stderr)
    return 1


def cmd_install_release(args: argparse.Namespace) -> int:
    try:
        releases = fetch_releases(args.repo, args.token)
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        print(f"ERROR: Failed to fetch GitHub Releases from {args.repo}: {exc}", file=sys.stderr)
        return 1

    rel = select_release(releases, args.tag, args.channel)
    if not rel:
        print(f"ERROR: Release not found (tag={args.tag}, channel={args.channel}).", file=sys.stderr)
        return 1

    tag = str(rel["tag_name"])
    port = args.port
    detected_chip: str | None = None
    detected_flash_mb: int | None = None

    if not args.dry_run:
        if not port:
            found = discover_serial_ports()
            if len(found) == 1:
                port = found[0]
                print(f"✅ Auto-selected USB serial port: {port}")
            elif len(found) > 1:
                print("❌ Multiple serial ports detected. Pass PORT=<path>:", file=sys.stderr)
                for p in found:
                    print(f"   - {p}", file=sys.stderr)
                return 1
            else:
                print("❌ No USB serial device detected. Plug in USB-C and try again.", file=sys.stderr)
                return 1

        if not args.no_probe:
            print(f"🔍 Probing ESP chip & Flash capacity on {port}...")
            detected_chip, detected_flash_mb, _ = probe_device(port)
            if detected_chip or detected_flash_mb:
                print(
                    f"✅ Hardware detected: MCU={detected_chip or '?'} | "
                    f"Flash={f'{detected_flash_mb} MB' if detected_flash_mb else '?'}"
                )

    env_name = args.env
    if not env_name:
        recs = recommend_envs(detected_chip, detected_flash_mb)
        env_name = recs[0].env if recs else "lilygo-t-deck"
        print(f"✅ Auto-selected compatible firmware: {env_name}")

    spec = BOARD_SPECS.get(env_name)
    if spec and detected_flash_mb and spec.flash_mb > detected_flash_mb:
        print(
            f"❌ Refusing to flash {env_name} ({spec.flash_mb} MB) onto a {detected_flash_mb} MB chip!\n"
            f"   Compatible builds for your hardware:",
            file=sys.stderr,
        )
        for r in recommend_envs(detected_chip, detected_flash_mb):
            print(f"   - ENV={r.env} ({r.name} · {r.flash_mb} MB · {r.edition})", file=sys.stderr)
        return 1

    if spec and detected_chip and spec.mcu != detected_chip:
        print(
            f"❌ Refusing to flash {env_name} ({spec.mcu}) onto a {detected_chip} chip!",
            file=sys.stderr,
        )
        return 1

    fname = f"NezhaCyber-{env_name}.bin"
    assets = {str(a.get("name")): a for a in rel.get("assets", []) if isinstance(a, dict)}
    asset = assets.get(fname)
    if not asset or not asset.get("browser_download_url"):
        print(f"❌ Release {tag} does not contain asset '{fname}'.", file=sys.stderr)
        avail = [n for n in sorted(assets) if n.startswith("NezhaCyber-") and n.endswith(".bin") and not n.endswith("-ota.bin")]
        if avail:
            print("   Available images in this release:", ", ".join(avail), file=sys.stderr)
        return 1

    asset_size = int(asset.get("size") or 0)
    if detected_flash_mb and asset_size > detected_flash_mb * 1024 * 1024:
        print(
            f"❌ Asset {fname} ({fmt_size(asset_size)}) exceeds detected {detected_flash_mb} MB flash!",
            file=sys.stderr,
        )
        return 1

    print(
        f"📦 Release {tag} -> {fname} ({fmt_size(asset_size)}"
        + (f" / {spec.flash_mb} MB [{spec.edition}]" if spec else "")
        + ")"
    )
    if args.dry_run:
        print(f"Dry-run OK: would download {asset['browser_download_url']} and flash to {port or '<port>'}")
        return 0

    cache_path = Path(".pio") / "release_cache" / tag / fname
    if not cache_path.is_file() or cache_path.stat().st_size != asset_size:
        print(f"⬇️  Downloading {fname} from GitHub Release {tag}...")
        gh_download_file(str(asset["browser_download_url"]), cache_path, args.token)

    # Verify SHA-256 against SHA256SUMS.txt if published with the release
    sums_asset = assets.get("SHA256SUMS.txt")
    if sums_asset and sums_asset.get("browser_download_url"):
        try:
            sums_text = gh_fetch_text(str(sums_asset["browser_download_url"]), args.token)
            expected = None
            for line in sums_text.splitlines():
                parts = line.strip().split()
                if len(parts) >= 2 and parts[-1] == fname:
                    expected = parts[0].lower()
                    break
            if expected:
                actual = sha256_file(cache_path).lower()
                if actual != expected:
                    cache_path.unlink(missing_ok=True)
                    print(f"❌ SHA-256 mismatch for {fname}!\n   expected: {expected}\n   actual:   {actual}", file=sys.stderr)
                    return 1
                print(f"🔒 SHA-256 verified: {actual[:16]}...")
        except Exception as exc:
            print(f"⚠️  Skipped SHA-256 check ({exc})")

    ok, msg = validate_binary_fit(cache_path, env_name, None, detected_flash_mb, detected_chip)
    print(("✅ " if ok else "❌ ") + msg)
    if not ok:
        return 1

    esptool = find_esptool_cmd()
    if not esptool:
        print("❌ Neither esptool nor PlatformIO was found.", file=sys.stderr)
        return 1

    chip_arg = detected_chip or (spec.mcu if spec else "auto")
    if args.erase:
        print(f"🧹 Erasing flash on {port} (chip={chip_arg})...")
        subprocess.run([*esptool, "--chip", chip_arg, "--port", port, "erase-flash"], check=True)

    print(f"⚡ Flashing {cache_path} to {port} (chip={chip_arg}, baud={args.baud})...")
    subprocess.run(
        [*esptool, "--chip", chip_arg, "--port", port, "--baud", str(args.baud), "write-flash", "0x00000", str(cache_path)],
        check=True,
    )
    print(f"\n🎉 Installed Nezha Cyber {tag} ({env_name})! Press Reset or power-cycle your device.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Nezha Cyber Smart Firmware Installer")
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--token", default=os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
    parser.add_argument("--list-releases", action="store_true", help="List GitHub Releases with per-board memory sizes")
    parser.add_argument("--install-release", action="store_true", help="Download and flash a firmware from GitHub Releases")
    parser.add_argument("--check-bin", metavar="PATH", help="Validate a local merged .bin against board flash limits")
    parser.add_argument("--ota-bin", metavar="PATH", help="Optional app-only firmware.bin for partition limit check")
    parser.add_argument("--probe-port", metavar="PORT", help="Probe connected chip & flash size via esptool")
    parser.add_argument("--env", default="", help="Target board environment (e.g. lilygo-t-deck, m5stack-cplus1_1)")
    parser.add_argument("--tag", default="latest", help="Release tag (e.g. v2.1.0, latest, beta)")
    parser.add_argument("--channel", choices=["all", "stable", "beta"], default="all")
    parser.add_argument("--port", default="", help="Serial port path")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--erase", action="store_true", help="Erase flash before writing")
    parser.add_argument("--no-probe", action="store_true", help="Skip esptool flash-id hardware probe")
    parser.add_argument("--dry-run", action="store_true", help="Resolve release & validate without flashing")
    parser.add_argument("--detected-flash-mb", type=int, default=None)
    parser.add_argument("--detected-chip", default=None)
    args = parser.parse_args()

    if args.list_releases:
        return cmd_list_releases(args)
    if args.check_bin:
        return cmd_check_bin(args)
    if args.probe_port:
        return cmd_probe(args)
    if args.install_release:
        return cmd_install_release(args)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
