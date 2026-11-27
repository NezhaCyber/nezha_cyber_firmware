# Roadmap — Nezha Cyber (哪吒网络安全)

This document tracks the active product direction of **Nezha Cyber (哪吒网络安全)**, maintained by **钟智强 (ctkqiang)**.

## Completed Milestones
- [x] **哪吒魔童 Brand & Industrial OS Redesign**: 20 curated 哪吒魔童 theme packs (71 total palettes), live visual Theme Studio, 3 selectable Motong UI crests, and 5-second animated cyber boot splash.
- [x] **7 Launcher Styles**: Grid, Carousel, List, Cards, Dock, Split, and Compact Grid with full touch, trackball, keyboard, and 3-button ergonomics.
- [x] **Original Standalone Nezha Cyber Pet (哪吒宠物)**: Persistent NVS companion with 4 visual evolution realms, tear-free PSRAM sprite animation, and passive RF spirit patrol.
- [x] **Flipper-Style Cyber Arsenal**: Sub-GHz Frequency Analyzer, Universal IR Remote, GPIO Pin Controller & Voltage Monitor, Live 2.4 GHz Wi-Fi Thermal Waterfall, Pocket GPIO Oscilloscope, Nezha Pentatonic Synth, Offline mbedTLS Crypto Lab, TOTP 2FA Authenticator, PWM Signal Gen, Morse TX, TRNG PassGen, and Dice Roller.
- [x] **Universal UART Terminal Pro (`feat/uart-terminal-pro`)**: Live partial-line prompts, ANSI escape stripping, HEX view/TX, Auto-Baud detection, USB-C ↔ UART passthrough bridge, SD logging, T-Deck Plus GPS 38400 preset, and out-of-the-box Grove defaults across M5Stack boards.
- [x] **Hardware & Protocol Engineering Suite (`feat/hardware-protocol-suite`)**: Logic Analyzer (8/16 MB) with on-device UART/I2C/SPI/1-Wire decoders & PulseView `.vcd` export, SPI NOR Flash & I2C 24Cxx EEPROM SD dumpers with CRC32 verification, and native ESP32 TWAI CAN Bus / OBD-II sniffer.
- [x] **Wireless Sentinel (`feat/wireless-sentinel`)**: Blue-team counter-surveillance engine detecting Wi-Fi Deauth/Evil-Twin/Probe storms, persistent BLE Stalker-Trackers (AirTag/SmartTag/Tile) & Skimmers, and CC1101 Sub-GHz jamming with SD incident logging.
- [x] **Platform & Ecosystem Upgrades (`feat/platform-ecosystem`)**: Live System Monitor HUD (`乾坤仪`) + `sysmon` JSON CLI, JSON Workflow Playbooks (`灵珠剧本`), and zero-build WebSerial Flasher & Live Companion WebApp (`tools/web_companion/`).
- [x] **Memory Architecture & Code Audit (`fix/memory-arch-audit`)**: Deep firmware memory/concurrency audit and bilingual engineering guides (`docs/MEMORY_AND_DATA_STRUCTURES*.md`, `docs/ARCHITECTURE*.md`).
- [x] **Open-Source Standardization**: Clean directory layout (`scripts/build/`, `partitions/`, `docs/`), automated GitHub CI/CD targeting `development`, tagged release pipelines, and bilingual AI contributor rules (`AGENTS.md` / `AGENTS_ZH.md`).

## Upcoming Focus
- [ ] **GitHub Pages Auto-Deploy for WebSerial Companion**: Publish `tools/web_companion/` with auto-generated `manifests/*.json` per GitHub Release tag.
- [ ] **BLE & Sub-GHz Protocol Expansion**: Expanded Rolling-Code analysis and custom `.sub` preset library.
- [ ] **Nezha Pet Companion Mini-Games & Achievements**: Additional unlockable cyber badges and companion interactions.
- [ ] **Automated Hardware-in-the-Loop Smoke Tests**: Serial CLI smoke test suite (`sysmon`, `wire`, `playbooks`) for ESP32-S3 and ESP32 targets.
