# Roo Code / DeepSeek Rules — Nezha Cyber (哪吒网络安全)

> Canonical Rules / 权威规范: [`AGENTS.md`](../../AGENTS.md) (English) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md) (简体中文)

## Core Guardrails / 核心工程红线

- **No AI Slop / 严禁垃圾代码**: Read real files (`src/`, `include/`, `boards/`, `platformio.ini`) first. Produce minimal, task-scoped diffs with zero stubs, zero mocked hardware outputs, and zero unrelated refactors. Never commit `NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `include/webFiles.h`, or `include/current_year.h`.
- **Branding / 品牌与兼容**: User-facing strings/UI use **Nezha Cyber (哪吒网络安全)** / **哪吒魔童** (author **钟智强 / ctkqiang**, **GNU AGPL-3.0**). Keep `THIRD_PARTY.md` intact and do not mass-rename internal C++ identifiers (`BruceConfig`, `bruce_i18n`, `/bruce.conf`).
- **Dual Flash & DMA Safety / 双档 Flash 与内存安全**:
  - Maintain clean builds on both `lilygo-t-deck` (16 MB ESP32-S3, PSRAM, `320×240`) and `m5stack-cplus1_1` (4 MB ESP32, no PSRAM, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` ceiling).
  - Guard heavy code/tables with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)` and store read-only tables in `PROGMEM`.
  - Always gate Wi-Fi/BLE init via `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` (`src/core/radio_mem.h`).
  - Maintain strict ODR (`-flto -Werror=odr`), SPI bus isolation (`sdcardSPI` vs `AUX_SPI`), `inputLock()`/`inputUnlock()`, and never alter `struct keyStroke` (`include/globals.h`).
- **Architecture & Data Structures / 架构与数据结构**: Respect layering (`menu_items`/`serial_commands` → `src/modules/` → `src/core/` → `bus_HAL`/`boards/*/interface.cc`). Always use a **local** `std::vector<Option>` in submenus (prevents lambda UAF), RAII `I2CLease` & `acquireSPIBus()`, `FS &fs` (`getFsStorage()`), bounded `MAX_*` POD scan tables, sorted `PROGMEM` tables, and the 4-phase lifecycle (Guard → Init → Dirty-Flag Loop → Deterministic Teardown). Avoid C++ exceptions, RTTI, `<iostream>`, and `std::regex`.
- **UI & i18n / 主题界面与双语字库**:
  - Use `src/core/display.h` theme helpers and `bruce_i18n::tr()` / UTF-8 width helpers.
  - Keep `src/core/lang_zh_cn.h` strictly ASCII `strcmp`-sorted. Never hand-edit `src/core/cjk_font_data.h`; run `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.
- **Style, Checks & Security / 风格、验证与安全**:
  - Format C++ to `.clang-format` (LLVM, 4 spaces, 110 cols).
  - Run `python3 tools/gen_cjk_font.py --check-tables`, `python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py`, and `pio run -e lilygo-t-deck -e m5stack-cplus1_1`, and report exact results honestly.
  - Branch off `development` and target all PRs to `development` (maintainer handles `development` → `main`) with atomic Conventional Commits and comply with `SECURITY.md`.
