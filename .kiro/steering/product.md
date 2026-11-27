---
inclusion: always
---

# Product Steering — Nezha Cyber (哪吒网络安全)

> Canonical specification / 权威规范: [`AGENTS.md`](../../AGENTS.md) (English) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md) (简体中文)

## Product Identity / 产品定位
- **Name**: **Nezha Cyber (哪吒网络安全)** — standalone ESP32 / ESP32-S3 / ESP32-C5 portable wireless security, Sub-GHz RF, IR, RFID/NFC, LoRa, and hardware diagnostic firmware.
- **Author & Maintainer**: **钟智强 (ctkqiang)**.
- **License & Lineage**: **GNU AGPL-3.0** ([LICENSE](../../LICENSE)). Forked from `pr3y/Bruce`; all upstream commit history and [THIRD_PARTY.md](../../THIRD_PARTY.md) notices **must** stay intact.

## Brand & Visual Identity / 品牌与视觉边界
- All **user-facing** copy, splash animations, menus, WebUI, default Wi-Fi AP (`NezhaNet` / `nezhanet`), WebUI login (`admin` / `nezha`), QR codes, and release binaries (`NezhaCyber-<env>.bin`) use **Nezha Cyber (哪吒网络安全)** and **哪吒魔童** visual styling.
- **Never** reintroduce legacy user-facing `Brucegotchi`, `PwnGrid`, or `Megalodon` names (replaced by original persistent **Nezha Pet / 哪吒宠物** in `src/modules/others/nezha_pet.*`).
- **Never** mass-rename legacy internal C++ identifiers (`BruceConfig`, `bruceConfig`, `bruce_i18n`, `BRUCE_COMPACT_CJK_FONT`, `BRUCE_VERSION`, `/bruce.conf`) unless explicitly requested.

## Primary Verified Hardware Tiers / 主力验证硬件档位
1. **16 MB Flagship**: `lilygo-t-deck` / `lilygo-t-deck-pro` — ESP32-S3, 16 MB Flash, 8 MB OPI PSRAM, `320×240`, GT911 touch + trackball + QWERTY keyboard (`NO_TOUCH_FOOTER=1`).
2. **4 MB Compact**: `m5stack-cplus1_1` / `CYD-2432S028` — ESP32, 4 MB Flash (`0x3E8000` app limit), **no PSRAM**, `BRUCE_COMPACT_CJK_FONT=1`, `240×135`, 3 buttons (`HAS_3_BUTTONS`).
3. **4 MB Lite / Launcher**: `LAUNCHER_m5stack-cplus1_1` / `LAUNCHER_CYD-2432S028` — `LITE_VERSION=1`.

## Core Feature Domains & Localization
- **Default Language**: Simplified Chinese (`zh-CN`), dynamically switchable to English (`en`) via `bruce_i18n`.
- **Feature Domains**: 7 Main Menu styles & 哪吒魔童 Theme Studio (20 curated themes / 71 palettes), Wi-Fi & Thermal Waterfall, BLE Suite, Live Tactical Radar, Sub-GHz/CC1101 & Universal IR, RFID/NFC (PN532/ST25R3916/MFRC522/Chameleon/Amiibolink), Wire Tools (GPIO/Scope/UART/I2C/SPI/JTAG/Continuity), Flipper-style daily utilities (TOTP, PWM, Morse, Crypto Lab, Synth), and Nezha Pet.

## Authorized Use & Security Red Lines / 安全合规红线
- Strictly for **authorized security research, education, hardware debugging, and personal lab testing** ([SECURITY.md](../../SECURITY.md)).
- Never hardcode real credentials, API tokens (`wigleBasicToken`, `wdgwarsApiKey`), weaken WebUI session auth, or add covert exfiltration/bricking logic.
