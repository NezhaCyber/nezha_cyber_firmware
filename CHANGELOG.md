# Changelog — Nezha Cyber (哪吒网络安全)

All notable changes to **Nezha Cyber (哪吒网络安全)** are documented in this file.  
This project preserves the full upstream Git history of [Bruce Firmware](https://github.com/pr3y/Bruce) as an independent standalone fork maintained by **钟智强 (ctkqiang)**.

---

## [Unreleased]

### Added
- **Logcat-style real-time system logger**: `/NezhaLogs/nezha.log` is appended
  and flushed on every line (no RAM history), with `V/D/I/W/E/F` levels, tags,
  task names, wall-clock/uptime timestamps, 512 KiB rotation, boot/reset-reason
  banners and abnormal-shutdown detection. Read it from `Config → System Log`
  (live on-device viewer), the serial CLI (`log tail|level|serial|info|clear`),
  or the WebUI (`/logs`, `/logs?html=1`).
- **Bilingual UI (`zh-CN` Default + English)**: Simplified Chinese is now the default firmware language with instant switching to English, backed by a compact PROGMEM CJK bitmap font (`tools/gen_cjk_font.py`).
- **5-Second Animated 哪吒魔童 Boot Splash**: Animated boot sequence with flanking sparks, corner brackets, subsystem ticker, progress bar, and startup chimes (skippable on any input).
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

### Fixed
- **Wi-Fi deauther correctness and crash fixes**: unchecked `sscanf` MAC parsing
  could inject uninitialised stack bytes; scan databases were deleted while menu
  callbacks still dereferenced `WiFi.BSSID(i)` (null dereference after the first
  attack); mesh mode targeted the *connected* SSID instead of the selected AP;
  "Deauth All by channel" sent frames with `FF:FF:...` BSSIDs and did nothing;
  target-AP client discovery added the AP itself as a client; Wi-Fi AP state was
  not restored after an attack; 5 GHz channels were attempted on 2.4 GHz-only
  radios. Deauth frames now use correct sequence-control byte order, raw-TX
  failures are counted and logged, and scans are released with `scanDelete()`.
- **IR fixes**: a corrupt/malicious `.ir` `bits:` value could overflow a VLA on
  the stack in `sendDecodedCommand` (now a fixed 512-byte state buffer with
  bounds checks); `malloc` failure in raw send was unchecked; the `.ir` replay
  parser swallowed the next `type:` block when no `#` separator was present
  (now a single-pass state machine); `IRCode.bits` widened to 16-bit so AC
  states >255 bits survive; TV-B-Gone batches bounds-check `numpairs`; IR
  receiver is constructed only after the RX pin is validated.
- **Memory/robustness**: `stringToMAC` no longer leaves uninitialised bytes;
  WebUI server allocation is null-checked; Wi-Fi attack menus release scan
  results; deauth-flood loop yields to the RTOS when no APs are found.
- **Crash hardening across modules**: bounded SMB/NTLM parsing in Responder;
  idempotent BLE client registration and de-duplicated cleanup (no double free);
  BLE file-transfer size-underflow and session lifetime guards; RFID `.rfid`
  UID/page length clamps (removed attacker-sized VLAs); RF `.sub` key/bit vector
  bounds; checked `calloc` for RAW RF; JS `JS_ToInt32` short-width overwrite,
  bitmap size overflow and dialog string lifetime fixes; serial CLI no longer
  aborts on malformed numbers or failed allocations; Evil Portal credential
  buffer is mutex-protected; WireGuard reports real `begin()` failures.

### Removed
- Removed legacy Bruce firmware `pwnagotchi` / `pwngrid` and `Megalodon` (`tururururu`) modules in favor of the original standalone **Nezha Cyber Pet**.
- Replaced legacy Bruce CI workflows with clean open-source Nezha Cyber workflows.
