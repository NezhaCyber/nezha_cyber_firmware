# Architecture & Repository Layout — Nezha Cyber (哪吒网络安全)

**Author & Maintainer:** 钟智强 ([ctkqiang](https://github.com/ctkqiang))  
**License:** GNU AGPL-3.0

---

## Standardized Directory Tree

```text
.
├── .github/                  # Open-source CI/CD workflows, Dependabot, Issue & PR templates
│   ├── ISSUE_TEMPLATE/       # Structured YAML issue forms (Bug, Feature, Board Support)
│   ├── workflows/            # ci.yml, release.yml, manual-board-build.yml, stale.yml
│   ├── PULL_REQUEST_TEMPLATE.md
│   └── dependabot.yml
├── boards/                   # Per-board PlatformIO definitions, pinouts & hardware interfaces
├── docs/                     # Architecture, roadmap & developer documentation
├── embedded_resources/       # WebUI static assets (auto-gzipped into include/webFiles.h)
├── include/                  # Shared headers & global hardware abstraction definitions
├── lib/                      # Vendored / patched embedded libraries
├── media/                    # Official 哪吒魔童 artwork, banners & hardware diagrams
├── partitions/               # Custom ESP32 / ESP32-S3 / ESP32-C5 Flash partition tables
│   ├── custom_4Mb.csv
│   ├── custom_4Mb_full.csv
│   ├── custom_4Mb_fullv2.csv
│   ├── custom_8Mb.csv
│   └── custom_16Mb.csv
├── pcbs/                     # Open-source PCB designs & hardware schematics
├── projects/                 # Standalone companion ESP32 projects (e.g. T-Deck Internet Radio)
├── scripts/
│   └── build/                # PlatformIO pre/post build hooks (submodule check, patch, merge-bin)
├── sd_files/                 # Reference microSD card templates (scripts, IR, portals, themes)
├── src/
│   ├── core/                 # Core OS: display, themes, i18n, config, menu system, WebUI, CLI
│   │   ├── i18n/             # Extended multi-language locale tables (KO/JA/DE/RU/ES/FR)
│   │   ├── menu_items/       # Top-level module launchers & menus
│   │   ├── serial_commands/  # Interactive serial CLI handlers
│   │   ├── tftLogger/        # Display wrapper & PROGMEM Unicode/CJK bitmap renderer
│   │   └── wifi/             # Wi-Fi manager, WebUI server & WireGuard helpers
│   ├── modules/              # Feature modules (WiFi, BLE, RF, RFID, IR, Radar, Flipper Tools, Nezha Pet)
│   └── main.cc               # Boot sequence, 5s animated 哪吒魔童 splash & main event loop
├── tools/                    # Host developer utilities (gen_cjk_font.py, asset converters)
├── CHANGELOG.md
├── CODE_OF_CONDUCT.md
├── CONTRIBUTING.md
├── LICENSE
├── Makefile                  # Auto-detecting local build & flash developer workflow
├── platformio.ini            # Master PlatformIO build configuration
├── README.md                 # English flagship documentation
├── README_ZH.md              # Simplified Chinese (简体中文) flagship documentation
├── SECURITY.md               # Responsible vulnerability disclosure & authorized-use policy
└── THIRD_PARTY.md            # Third-party libraries & upstream Bruce Firmware attribution
```

---

## Key Subsystems

### 1. Display, Theme Studio & 哪吒魔童 UI (`src/core/display.*`, `src/core/settingsColor.h`)
- Unified dark-obsidian hardware UI helpers (`uiSurfaceColor`, `uiBorderDimColor`, `uiTextBrightColor`, `uiTextOnAccentColor`, `uiDrawText16`).
- **20 curated 哪吒魔童 (Nezha Demon Child) theme packs** (`71` total palettes) + interactive full-screen **Theme Studio** (`setNezhaThemeGallery()`).
- **3 selectable 哪吒魔童 UI crests** (`Motong Flame`, `Lotus Seal`, `Ice-Fire Twin`) + **7 Main Menu layouts** (`src/core/main_menu.*`).

### 2. Bilingual i18n & CJK Bitmap Font Pipeline (`src/core/i18n.*`, `tools/gen_cjk_font.py`)
- **2 supported languages**: Simplified Chinese (`zh-CN`, default) and English (`en`).
- **Flash-budget tiering**:
  - 4 MB boards (`BRUCE_COMPACT_CJK_FONT`, e.g. `m5stack-cplus1_1`): compiles 12×12 PROGMEM CJK bitmaps so the full feature set fits inside `0x3E8000`.
  - 8 MB / 16 MB boards (e.g. `lilygo-t-deck`): compiles both 12×12 and 16×16 PROGMEM CJK bitmaps.
- `src/core/lang_zh_cn.h` is verified strictly ASCII-sorted for O(log N) binary search via `python3 tools/gen_cjk_font.py --check`.

### 3. Build & Partition Pipeline (`scripts/build/`, `partitions/`, `Makefile`)
- `scripts/build/build.py` validates firmware size against the target partition table and emits ready-to-flash merged binaries (`NezhaCyber-<env>.bin`).
- `Makefile` auto-detects connected USB serial ports and targets (`make tdeck`, `make stickc-plus`, `make build`, `make install`).

### 4. Real-Time Diagnostics (`src/core/logging/`, `docs/LOGGING.md`)
- Android-logcat-style `V/D/I/W/E/F` logger with fixed 320-byte stack lines, no RAM history and immediate SD append/flush to `/NezhaLogs/nezha.log`.
- 512 KiB rotation, boot/reset-reason/crash banner, heap/PSRAM snapshots, recursive-mutex task safety.
- On-device live viewer (`Config → System Log`), serial CLI (`log tail/level/info/clear`) and WebUI (`/logs`, `/logs?html=1`).
