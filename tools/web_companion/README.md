# Nezha Cyber — WebSerial Flasher & Live Companion (`tools/web_companion/`)

> **English & 简体中文** zero-build browser companion for **Nezha Cyber (哪吒网络安全)**.

## Features
1. **Live USB-C Serial CLI & Remote Navigation**: Connect directly over USB-C using Chrome / Edge WebSerial (no Wi-Fi required), run commands with history, and drive the device UI remotely via `nav` D-Pad buttons.
2. **Live Health HUD (`乾坤仪`)**: Polls `sysmon` JSON over USB-C to display real-time Internal DRAM, Largest Contiguous DMA Block (with Wi-Fi/BLE readiness pills), PSRAM, LittleFS/SD status, Battery %, and FreeRTOS task metrics.
3. **Storage Browser & Playbook Runner (`灵珠剧本`)**: Browse SD/LittleFS directories (`storage list`), author `/NezhaPlaybooks/*.json` workflows, execute them step-by-step over USB-C, or download `.json` files to your microSD card.
4. **One-Click WebSerial Flasher**: Ready for `esp-web-tools` release manifests (`manifests/*.json`) pointing to merged `NezhaCyber-<env>.bin` binaries.

## Quick Local Launch
```bash
cd tools/web_companion
python3 -m http.server 8080
```
Open `http://localhost:8080` in **Chrome** or **Edge** (WebSerial requires `localhost` or HTTPS) and click **Connect USB-C**.
