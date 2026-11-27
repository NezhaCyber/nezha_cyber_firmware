# GitHub Copilot Instructions — Nezha Cyber (哪吒网络安全)

> Canonical specification: [`AGENTS.md`](../AGENTS.md) (English) | [`AGENTS_ZH.md`](../AGENTS_ZH.md) (简体中文).

## Project Context
**Nezha Cyber (哪吒网络安全)** is an AGPL-3.0 ESP32 / ESP32-S3 / ESP32-C5 portable wireless security, RF, IR, RFID, and hardware utility firmware maintained by **钟智强 (ctkqiang)** (forked from `pr3y/Bruce`).

## Mandatory Engineering Guardrails

1. **Anti-Slop Policy**
   - Inspect real files in `src/`, `include/`, `boards/`, and `platformio.ini` before writing code. Never invent pins, macros, headers, or APIs.
   - Keep edits minimal and strictly scoped. Do not refactor unrelated modules, reformat untouched files, or emit `// TODO` stubs or mocked hardware outputs.
   - Never commit build/generated outputs (`NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `include/webFiles.h`, `include/current_year.h`).

2. **Branding & Compatibility**
   - User-facing copy & UI use **Nezha Cyber (哪吒网络安全)** and **哪吒魔童** styling. Do not reintroduce legacy `Brucegotchi` / `PwnGrid` / `Megalodon` branding.
   - Preserve upstream attribution in `THIRD_PARTY.md` / `LICENSE`. Do not mass-rename legacy internal C++ identifiers (`BruceConfig`, `bruce_i18n`, `BRUCE_COMPACT_CJK_FONT`, `/bruce.conf`).

3. **ESP32 / ESP32-S3 Memory & Flash Constraints**
   - Maintain compatibility across **16 MB PSRAM** (`lilygo-t-deck`, `320×240`) and **4 MB no-PSRAM** (`m5stack-cplus1_1`, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` app ceiling) plus `LITE_VERSION` targets.
   - Guard heavy assets/tables with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)` and keep read-only tables in `PROGMEM`.
   - Always gate Wi-Fi/NimBLE initialization through `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` in `src/core/radio_mem.h`.
   - Respect `-flto -Werror=odr`, SPI host isolation (`sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)`), `inputLock()`/`inputUnlock()`, and never modify `struct keyStroke` in `include/globals.h`.

4. **Architecture, Data Structures & Coding Patterns**
   - Follow strict layering (`menu_items`/`serial_commands`/`WebUI` → `src/modules/` → `src/core/` → `bus_HAL`/`boards/*/interface.cc`) and keep shared parsers in `src/modules/` or `src/core/`.
   - Always build a **local** `std::vector<Option>` inside submenus (never reuse global `options` in nested menus — avoids lambda UAF).
   - Acquire I2C via move-only RAII `I2CLease` (`src/core/bus_HAL.h`), SPI via `acquireSPIBus()`, and storage via `FS &fs` (`getFsStorage()`).
   - Cap scan caches with `MAX_*` + fixed POD fields, store static tables in sorted `PROGMEM` arrays, follow the 4-phase lifecycle (Guard → Init → Dirty-Flag Loop → Deterministic Teardown), and avoid C++ exceptions, RTTI, `<iostream>`, and `std::regex`.

5. **UI & Bilingual i18n (`zh-CN` default + `en`)**
   - Use `src/core/display.h` theme helpers (`uiSurfaceColor()`, `uiBorderDimColor()`, `uiTextBrightColor()`, `uiTextOnAccentColor()`, `uiDrawText16()`) and responsive coordinates (`tftWidth`, `tftHeight`).
   - Route UI strings through `bruce_i18n::tr()`. Keep `src/core/lang_zh_cn.h` strictly ASCII `strcmp`-sorted. Never hand-edit `src/core/cjk_font_data.h`; regenerate with `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.

6. **Style, Build Verification & Security**
   - Follow `.clang-format` (LLVM, 4 spaces, 110 columns, attached braces) for `.cc`/`.h` and Python 3.11+ (`py_compile`) for build/host scripts.
   - Required verification commands:
     - `python3 tools/gen_cjk_font.py --check-tables`
     - `python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py`
     - `pio run -e lilygo-t-deck -e m5stack-cplus1_1`
   - Follow `SECURITY.md` (authorized security research/testing only; never hardcode secrets or weaken WebUI auth).
