# Changelog — Nezha Cyber (哪吒网络安全)

All notable changes to **Nezha Cyber (哪吒网络安全)** are documented in this file.  
This project preserves the full upstream Git history of [Bruce Firmware](https://github.com/pr3y/Bruce) as an independent standalone fork maintained by **钟智强 (ctkqiang)**.

---

## [Unreleased]

### Added
- **8-Language Internationalization (i18n)**: Added Korean (`한국어`), Japanese (`日本語`), German (`Deutsch`), Russian (`Русский`), Spanish (`Español`), and French (`Français`) alongside English and Simplified Chinese (`简体中文`), backed by an automated multi-script PROGMEM Unicode bitmap font generator (`tools/gen_cjk_font.py`).
- **5-Second Animated 哪吒魔童 Boot Splash**: Swirling Wind-Fire ember sparks, pulsing Qiankun corner brackets, dual-side reactor bars, 6-stage subsystem boot ticker, dual-tone Wind-Fire progress bar, and pentatonic chimes (instantly skippable on any input).
- **4 New Cyber Tools in Flipper Tools**:
  - **WiFi Waterfall (`WiFi 瀑布图`)**: Live 2.4 GHz 13-channel thermal bar spectrum, scrolling 48-row heat waterfall, and quietest best-channel recommender.
  - **GPIO Scope (`GPIO 示波器`)**: Real-time pocket ADC/Logic oscilloscope with graticule grid, `Min/Max/Avg/Hz`, timebase, and Hold trigger.
  - **Nezha Synth (`哪吒合成器`)**: Playable 8-pad Chinese Pentatonic (`宫商角徵羽`), Chromatic & 8-FX cyber synthesizer with live visualizer bars.
  - **Cyber Crypto (`密码实验室`)**: Offline mbedTLS SHA-256 / SHA-1 / MD5 / Base64 / ESP32 TRNG hash lab.
- **20 Curated 哪吒魔童 Theme Packs & Live Theme Studio**: 71 total built-in color palettes, full-screen live preview gallery (`Config → Display & UI → Nezha Themes`), and 3 selectable UI crests (`Motong Flame`, `Lotus Seal`, `Ice-Fire Twin`).
- **Original Standalone Nezha Cyber Pet (哪吒宠物)**: Persistent NVS companion with 4 visual evolution realms, tear-free PSRAM sprite rendering on T-Deck, and passive ambient RF spirit patrol.
- **Flipper Zero Feature Suite**: Sub-GHz Frequency Analyzer, Universal IR Remote, GPIO Pin Controller & Voltage Monitor, TOTP 2FA Authenticator, PWM Signal Gen, Morse TX, TRNG PassGen, and Polyhedral Dice Roller.
- **7 Main Menu Layouts**: Grid, Carousel, List, Cards, Dock, Split, and Compact Grid.
- **Tactical Radar Sonar Beep**: Global Audio Config toggle (`Radar Beep`, default ON) and threat flagging.
- **Standardized Open-Source Project Structure & CI/CD**:
  - Organized build hooks into `scripts/build/`, partition tables into `partitions/`, and architecture/roadmap into `docs/`.
  - Added `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `CHANGELOG.md`, Dependabot, structured YAML issue templates, PR template, and redesigned `.github/workflows/` (`ci.yml`, `release.yml`, `manual-board-build.yml`, `stale.yml`).

### Changed
- Full rebrand to **Nezha Cyber (哪吒网络安全)** with author attribution to **钟智强 (ctkqiang)** and standalone fork documentation in `README.md` and `README_ZH.md`.
- Upgraded merged firmware artifacts to `NezhaCyber-<env>.bin`.
- Unified industrial dark-obsidian hardware UI across menus, BLE Spam, keyboard, settings, WebUI, and dialogs.

### Removed
- Removed legacy Bruce firmware `pwnagotchi` / `pwngrid` and `Megalodon` (`tururururu`) modules in favor of the original standalone **Nezha Cyber Pet**.
- Replaced legacy Bruce CI workflows with clean open-source Nezha Cyber workflows.
