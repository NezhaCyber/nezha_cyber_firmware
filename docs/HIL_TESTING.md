# Hardware-in-the-Loop (HIL) Smoke Testing & WebSerial Release Guide

> **Language / 语言:** **English** | [简体中文 (Simplified Chinese)](HIL_TESTING_ZH.md)

This guide covers automated USB-C regression testing (`tools/hil_smoke_test.py`) and automated GitHub Pages deployment for the **Nezha Cyber (哪吒网络安全)** WebSerial Flasher & Live Companion.

---

## 1. Automated USB-C HIL Smoke Test Suite

`tools/hil_smoke_test.py` connects to a flashed ESP32 / ESP32-S3 device over USB-C and verifies core firmware health before cutting a release:

| Check | CLI Command | Pass Criteria |
| :--- | :--- | :--- |
| Device Info | `info` | Non-empty board banner without `ERROR` |
| Uptime Clock | `uptime` | Valid monotonic clock response |
| Heap Report | `free` | Numeric free heap output |
| SysMon Telemetry & DMA Floor | `sysmon` | `heap_free >= min_heap_kb`, `dma_largest >= min_dma_kb`, `wifi_ok == true`, `cpu_mhz > 0` |
| Storage Root List | `storage list /` | Mounted FS returns entries cleanly |
| Wire UART Status | `wire uart status` | Reports `baud=` & pin status |
| Wire I2C Arbitration | `wire i2c scan` | Completes via `I2CLease` without bus lockup |

### Usage

```bash
# Install pyserial once on the host workstation
pip install pyserial

# Run against a connected LilyGO T-Deck / Cardputer / StickC Plus
python3 tools/hil_smoke_test.py --port /dev/ttyACM0 --baud 115200

# Strict 16 MB PSRAM board thresholds + JSON report output
python3 tools/hil_smoke_test.py \
  --port /dev/ttyACM0 \
  --min-heap-kb 64 \
  --min-dma-kb 24 \
  --json-out hil_report.json

# Self-test the runner without physical hardware (also executed in GitHub CI)
python3 tools/hil_smoke_test.py --mock
```

---

## 2. WebSerial Flasher Manifests & GitHub Pages Auto-Deploy

- **Manifest Generator**: `tools/gen_web_manifests.py` generates `esp-web-tools` v10 manifests (`<env>.json` + `boards.json`) for all 12 official Nezha Cyber release targets.
- **Automated Deployment**: `.github/workflows/pages.yml` runs automatically on every published GitHub Release (or manual `workflow_dispatch`), stages `tools/web_companion/`, builds manifests matching the release tag, and publishes to GitHub Pages.

### Local Generation & Preview

```bash
python3 tools/gen_web_manifests.py --out-dir tools/web_companion/manifests --tag latest
cd tools/web_companion
python3 -m http.server 8080
```
Open `http://localhost:8080` in Chrome or Edge to use the WebSerial Flasher, Live CLI, Remote D-Pad, and `sysmon` Health HUD.
