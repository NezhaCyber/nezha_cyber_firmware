<p align="center">
  <img src="./media/pictures/nezha_cyber_banner.jpg" alt="Nezha Cyber (哪吒网络安全)" width="200" />
</p>

<h1 align="center">Nezha Cyber (哪吒网络安全)</h1>

<p align="center">
  ESP32 / ESP32-S3 portable wireless security, RF, and hardware utility firmware.<br />
  <strong>English</strong> | <a href="./README_ZH.md">简体中文</a>
</p>

<p align="center">
  Author: <strong>钟智强 (<a href="https://github.com/ctkqiang">ctkqiang</a>)</strong> · License: <strong>GNU AGPL-3.0</strong>
</p>

---

## About

**Nezha Cyber (哪吒网络安全)** is a standalone firmware project forked from [Bruce Firmware](https://github.com/pr3y/Bruce). All upstream Git commit history and credits are preserved, while the codebase diverges independently due to a different design philosophy:

- **Default Simplified Chinese + English UI**: Ships in Simplified Chinese (`zh-CN`) by default with instant switching to English (`en`), powered by a compact 12px/16px PROGMEM CJK bitmap font pipeline that still fits on 4 MB ESP32 boards.
- **Unified Hardware UI & 哪吒魔童 Themes**: Rebuilt launcher, menus, dialogs, keyboard, BLE tools, and settings with 20 哪吒魔童 color presets (71 palettes total), a live Theme Studio, 3 subtle UI crest styles, 7 main menu layouts, and a 5-second animated boot splash (skippable on any key/touch).
- **Standalone Companion & Daily Hardware Tools**: Replaces legacy `Brucegotchi` / `PwnGrid` / `Megalodon` with an original persistent **Nezha Pet (哪吒宠物)**, and adds Flipper-style daily utilities (Sub-GHz frequency analyzer, universal IR remote, GPIO controller & scope, Wi-Fi thermal waterfall, TOTP 2FA, PWM generator, Morse TX, offline crypto hashes, and pentatonic synth).

---

## Features

| Category | Modules |
| :--- | :--- |
| **UI & Themes** | 7 Main Menu layouts (`Grid`, `Carousel`, `List`, `Cards`, `Dock`, `Split`, `Compact Grid`), 哪吒魔童 Theme Studio (20 curated presets, 71 palettes total), 3 UI crest styles, `zh-CN` (default) & `en` |
| **Diagnostics** | Logcat-style real-time system log on SD (`/NezhaLogs/nezha.log`), on-device live viewer, serial CLI (`log tail/level/info/clear`), WebUI `/logs` + auto-refresh viewer, reset-reason/crash detection |
| **Wi-Fi** | STA/AP, Raw Sniffer, Channel Analyzer, 2.4 GHz Thermal Waterfall, Jam Detect, Probe Monitor, LAN Port/Banner Audit, IDS Watchdog, Evil Portal, Responder, ARP Scan, WireGuard, SSH/Telnet, SOCKS4 |
| **BLE** | Scrollable BLE Spam suite, BLE Scanner, GATT Inspector & Notify Viewer, BLE File Transfer, BadBLE & BLE Keyboard |
| **Radar** | Live Wi-Fi & BLE spatial radar with threat flags, persistent GPS/RF location state, and toggleable Sonar Beep |
| **Sub-GHz & IR** | CC1101 / RF433 Frequency Analyzer, Spectrum, Waterfall, Record/Replay, ProtoPirate, Universal IR Remote, TV-B-Gone, Custom IR |
| **RFID / Radio** | PN532, PN532Killer, Chameleon, Amiibolink, LoRa, FM Radio, NRF24 2.4G Spectrum |
| **Hardware & Daily Tools** | GPIO Pin Controller & ADC Voltage Monitor, Pocket GPIO Oscilloscope, UART / I2C / SPI / JTAG / Continuity Multimeter, TOTP 2FA, PWM Signal Gen, Morse TX, Offline Crypto Lab (`SHA-256`/`SHA-1`/`MD5`/`Base64`), TRNG PassGen, Nezha Synth, QR Codes, BadUSB/HID, JS Interpreter, Nezha Pet |

---

## Primary Verified Targets

| Board | MCU | Flash / PSRAM | PlatformIO Env |
| :--- | :--- | :--- | :--- |
| **LilyGO T-Deck / T-Deck Plus** | ESP32-S3 | 16 MB / 8 MB OPI PSRAM | `lilygo-t-deck`, `lilygo-t-deck-pro` |
| **M5StickC Plus 1.1** | ESP32 | 4 MB | `m5stack-cplus1_1` |

Additional board definitions from the wider ESP32 ecosystem (M5Stack Cardputer, Core2, CoreS3, M5StickC Plus2, LilyGO T-Embed CC1101, CYD-2432S028, ESP32-C5, etc.) are located under [`boards/`](./boards/).

---

## Install & Build

### 1. Prebuilt Releases
Download the latest merged binary (`NezhaCyber-<board>.bin`) or OTA image (`NezhaCyber-<board>-ota.bin`) from **GitHub Releases** and flash at `0x00000`:

```sh
# LilyGO T-Deck (ESP32-S3)
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x00000 NezhaCyber-lilygo-t-deck.bin

# M5StickC Plus 1.1 (ESP32)
esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash 0x00000 NezhaCyber-m5stack-cplus1_1.bin
```

### 2. Build from Source (`Makefile` or PlatformIO)
```sh
# Auto-detect connected T-Deck or M5StickC Plus 1.1, build, and flash
make install

# Target a specific board
make tdeck
make stickc-plus

# Build merged binary only -> NezhaCyber-<env>.bin
make build ENV=lilygo-t-deck
make build ENV=m5stack-cplus1_1

# Or directly with PlatformIO
pio run -e lilygo-t-deck -e m5stack-cplus1_1
```

**LilyGO T-Deck Download Mode:** Power OFF → hold Trackball button (`GPIO0`) → Power ON → release Trackball → run `make tdeck`.

### 3. Updating Chinese Translations
Edit [`src/core/lang_zh_cn.h`](./src/core/lang_zh_cn.h) (strict ASCII sort order), then regenerate and verify the CJK font header:
```sh
python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check
```

---

## Project Structure

See [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) for the full repository layout.

- `src/core/` — Core OS, display, themes, `zh-CN`/`en` i18n, config, launcher, WebUI, serial CLI
- `src/modules/` — Wireless, RF, IR, RFID, Wire Tools, Radar, Flipper Tools, Nezha Pet
- `boards/` — Hardware pinouts and PlatformIO board configs
- `partitions/` — Custom 4 MB / 8 MB / 16 MB partition CSV tables
- `scripts/build/` — PlatformIO pre/post build hooks and release packager
- `tools/` — Host utilities (`gen_cjk_font.py`)
- `.github/` — CI build matrix, automated GitHub Releases, issue & PR templates

More docs: [AGENTS.md](./AGENTS.md) · [CONTRIBUTING.md](./CONTRIBUTING.md) · [SECURITY.md](./SECURITY.md) · [CHANGELOG.md](./CHANGELOG.md) · [docs/ROADMAP.md](./docs/ROADMAP.md) · [Real-time logging](./docs/LOGGING.md) · [Hardware verification checklist](./docs/VERIFICATION.md)

---

## Acknowledgements & License

Maintained by **钟智强 ([ctkqiang](https://github.com/ctkqiang))**.  
Forked from **[pr3y/Bruce](https://github.com/pr3y/Bruce)** with gratitude to all upstream Bruce contributors and foundational projects (`ESP32Marauder`, `Launcher`, `Evil-M5Project`, `M5Stick-Nemo`). Third-party licenses and notices are listed in [THIRD_PARTY.md](./THIRD_PARTY.md).

Released under **GNU AGPL-3.0** ([LICENSE](./LICENSE)). Intended strictly for authorized security research, education, hardware debugging, and personal lab testing.
