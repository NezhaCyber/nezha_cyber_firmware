#!/usr/bin/env python3
"""Automated USB-C Hardware-in-the-Loop (HIL) smoke test suite for Nezha Cyber firmware.

Usage:
    python3 tools/hil_smoke_test.py --port /dev/ttyACM0
    python3 tools/hil_smoke_test.py --mock   # Self-test harness without hardware
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Protocol


class SerialTransport(Protocol):
    def send_and_read(self, command: str, wait_s: float = 0.6) -> str: ...
    def close(self) -> None: ...


class RealSerialTransport:
    def __init__(self, port: str, baud: int, timeout: float = 0.2) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError("pyserial is required for hardware testing: pip install pyserial") from exc
        self._ser = serial.Serial(port, baudrate=baud, timeout=timeout)
        time.sleep(0.3)
        self._ser.reset_input_buffer()

    def send_and_read(self, command: str, wait_s: float = 0.6) -> str:
        self._ser.reset_input_buffer()
        self._ser.write((command.strip() + "\n").encode("utf-8"))
        self._ser.flush()
        deadline = time.monotonic() + wait_s
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            data = self._ser.read(self._ser.in_waiting or 1)
            if data:
                chunks.append(data)
            else:
                time.sleep(0.02)
        return b"".join(chunks).decode("utf-8", errors="replace").strip()

    def close(self) -> None:
        self._ser.close()


class MockSerialTransport:
    """Deterministic mock device for CI self-testing the HIL suite."""

    RESPONSES = {
        "info": "Nezha Cyber (Lilygo T-Deck) ESP32-S3 SDK 5.1",
        "uptime": "Uptime: 00:05:12",
        "free": "Free heap: 184320 bytes",
        "sysmon": (
            '{"board":"Lilygo T-Deck","heap_free":184320,"heap_total":327680,"heap_min":150000,'
            '"dma_free":112640,"dma_largest":65536,"psram_free":8100000,"psram_total":8388608,'
            '"wifi_ok":true,"ble_ok":true,"wifi_mode":0,"ble_up":false,"webui":false,'
            '"sd":true,"lfs_used":24576,"lfs_total":196608,"bat":94,"cpu_mhz":240,'
            '"uptime_s":312,"tasks":11}'
        ),
        "storage list /": "DIR /NezhaLogs\nFILE /config.conf",
        "wire uart status": "baud=115200 frame=8N1 rx=44 tx=43 invert=0 active=0",
        "wire i2c scan": "FOUND 2 device(s):\n0x55 SYS\n0x5D SYS",
    }

    def send_and_read(self, command: str, wait_s: float = 0.05) -> str:
        return self.RESPONSES.get(command.strip(), "OK")

    def close(self) -> None:
        return None


@dataclass
class CheckResult:
    name: str
    command: str
    passed: bool
    detail: str
    duration_ms: int


def extract_json_line(output: str) -> dict | None:
    for line in output.splitlines():
        s = line.strip()
        if s.startswith("{") and s.endswith("}"):
            try:
                return json.loads(s)
            except json.JSONDecodeError:
                continue
    return None


def run_suite(
    transport: SerialTransport,
    min_heap_kb: int,
    min_dma_kb: int,
    require_sysmon: bool,
) -> list[CheckResult]:
    results: list[CheckResult] = []

    def exec_check(name: str, cmd: str, wait_s: float, validator) -> None:
        t0 = time.monotonic()
        out = transport.send_and_read(cmd, wait_s=wait_s)
        dt_ms = int((time.monotonic() - t0) * 1000)
        ok, detail = validator(out)
        results.append(CheckResult(name=name, command=cmd, passed=ok, detail=detail, duration_ms=dt_ms))

    exec_check(
        "Device Info",
        "info",
        0.5,
        lambda o: (len(o) > 0 and "ERROR" not in o.upper(), o.splitlines()[0][:80] if o else "empty response"),
    )

    exec_check(
        "Uptime Clock",
        "uptime",
        0.4,
        lambda o: ("uptime" in o.lower(), o.splitlines()[0][:80] if o else "empty response"),
    )

    exec_check(
        "Heap Report",
        "free",
        0.4,
        lambda o: (len(o) > 0 and any(ch.isdigit() for ch in o), o.splitlines()[0][:80] if o else "empty response"),
    )

    def validate_sysmon(out: str) -> tuple[bool, str]:
        data = extract_json_line(out)
        if data is None:
            if not require_sysmon:
                return (True, "sysmon not present on this build (skipped)")
            return (False, f"no JSON telemetry in response: {out[:80]!r}")
        heap_kb = int(data.get("heap_free", 0)) // 1024
        dma_kb = int(data.get("dma_largest", 0)) // 1024
        wifi_ok = bool(data.get("wifi_ok", False))
        cpu = int(data.get("cpu_mhz", 0))
        ok = heap_kb >= min_heap_kb and dma_kb >= min_dma_kb and wifi_ok and cpu > 0
        detail = f"DRAM={heap_kb}KB (min {min_heap_kb}KB) DMA_MAX={dma_kb}KB (min {min_dma_kb}KB) WIFI={wifi_ok} CPU={cpu}MHz"
        return (ok, detail)

    exec_check("SysMon Telemetry & DMA Floor", "sysmon", 0.5, validate_sysmon)

    exec_check(
        "Storage Root List",
        "storage list /",
        0.7,
        lambda o: ("ERROR" not in o.upper() and len(o) > 0, f"{len(o.splitlines())} line(s) returned"),
    )

    exec_check(
        "Wire UART Status",
        "wire uart status",
        0.4,
        lambda o: ("baud=" in o, o.splitlines()[0][:80] if o else "empty response"),
    )

    exec_check(
        "Wire I2C Bus Arbitration",
        "wire i2c scan",
        0.8,
        lambda o: ("FOUND" in o or "ERROR: set sda/scl" in o, o.splitlines()[0][:80] if o else "empty response"),
    )

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Nezha Cyber USB-C Hardware-in-the-Loop smoke test runner")
    parser.add_argument("--port", default=None, help="Serial port (e.g. /dev/ttyACM0, /dev/ttyUSB0, COM4)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--min-heap-kb", type=int, default=40, help="Minimum free internal DRAM in KB (default: 40)")
    parser.add_argument("--min-dma-kb", type=int, default=16, help="Minimum contiguous DMA block in KB (default: 16)")
    parser.add_argument("--allow-legacy-no-sysmon", action="store_true", help="Do not fail if firmware lacks sysmon")
    parser.add_argument("--json-out", type=Path, default=None, help="Optional JSON report output path")
    parser.add_argument("--mock", action="store_true", help="Run against built-in mock device (CI self-test)")
    args = parser.parse_args()

    if not args.mock and not args.port:
        parser.error("Provide --port <device> or --mock")

    try:
        transport: SerialTransport = (
            MockSerialTransport() if args.mock else RealSerialTransport(args.port, args.baud)
        )
    except Exception as exc:
        print(f"[FATAL] Failed to open transport: {exc}", file=sys.stderr)
        return 2

    try:
        results = run_suite(
            transport,
            min_heap_kb=args.min_heap_kb,
            min_dma_kb=args.min_dma_kb,
            require_sysmon=not args.allow_legacy_no_sysmon,
        )
    finally:
        transport.close()

    passed = sum(1 for r in results if r.passed)
    total = len(results)

    print(f"\nNezha Cyber HIL Smoke Test ({'MOCK' if args.mock else args.port})")
    print("-" * 78)
    for r in results:
        status = "PASS" if r.passed else "FAIL"
        print(f"[{status}] {r.name:<30} ({r.duration_ms:>4} ms) | {r.detail}")
    print("-" * 78)
    print(f"Summary: {passed}/{total} checks passed")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(
                {"mode": "mock" if args.mock else args.port, "passed": passed, "total": total, "results": [asdict(r) for r in results]},
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
