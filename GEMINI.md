# GEMINI.md — Gemini / Antigravity Agent Instructions for Nezha Cyber (哪吒网络安全)

> Canonical specification: [`AGENTS.md`](./AGENTS.md) (English) | [`AGENTS_ZH.md`](./AGENTS_ZH.md) (简体中文).  
> 本仓库权威规范请参阅 [`AGENTS.md`](./AGENTS.md) 与 [`AGENTS_ZH.md`](./AGENTS_ZH.md)。

## Non-Negotiable Rules / 不可违背的工程守则

- **Anti-Slop / 杜绝垃圾代码**: Inspect real code in `src/`, `include/`, `boards/`, and `platformio.ini` before changing anything. Make minimal, focused edits with zero `TODO` stubs, fake sensor/RF outputs, speculative wrappers, or unrelated reformatting.
- **Branding / 品牌边界**: Keep all user-facing text and visuals aligned with **Nezha Cyber (哪吒网络安全)** and **哪吒魔童** (maintained by **钟智强 / ctkqiang**, **GNU AGPL-3.0**). Keep upstream attribution in `THIRD_PARTY.md` and leave legacy internal C++ identifiers (`BruceConfig`, `bruce_i18n`, `/bruce.conf`) intact.
- **Dual Flash-Tier & DMA Safety / 双档 Flash 与内存安全**:
  - Changes must work on both 16 MB ESP32-S3 (`lilygo-t-deck`, PSRAM, `320×240`) and 4 MB ESP32 (`m5stack-cplus1_1`, no PSRAM, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` app ceiling).
  - Gate heavy features with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)`.
  - Gate Wi-Fi/BLE bring-up with `radioHasMemForWifi()` / `radioHasMemForBle()` / `radioSafeBleInit()` (`src/core/radio_mem.h`).
  - Maintain strict ODR (`-flto -Werror=odr`), SPI bus isolation (`sdcardSPI` vs `AUX_SPI`), and never alter `struct keyStroke` (`include/globals.h`).
- **Architecture, Data Structures & Coding Patterns / 架构与数据结构**:
  - Keep strict layering (`menu_items`/`serial_commands` → `src/modules/` → `src/core/` → `bus_HAL`/`boards/*/interface.cc`) and share protocol/file parsers across GUI & CLI.
  - Use a **local** `std::vector<Option>` inside submenus (never global `options` in nested menus — prevents lambda UAF).
  - Use RAII `I2CLease` (`i2cLeaseDevice`/`i2cLeaseBus`/`i2cLeasePins`), `acquireSPIBus()`, `FS &fs` (`getFsStorage()`), bounded `MAX_*` POD scan tables, sorted `PROGMEM` lookup arrays, and the 4-phase lifecycle (Guard → Init → Dirty-Flag Loop → Deterministic Teardown). Avoid C++ exceptions, RTTI, `<iostream>`, and `std::regex`.
- **UI & i18n / 主题界面与中文字库**:
  - Use `src/core/display.h` theme helpers (`uiSurfaceColor()`, `uiTextBrightColor()`, `uiDrawText16()`, etc.) and support both touch+trackball and 3-button ergonomics.
  - Keep `src/core/lang_zh_cn.h` strictly ASCII-sorted; never hand-edit `src/core/cjk_font_data.h` — run `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.
- **Style & Verification / 风格与验证**:
  - Follow `.clang-format` (LLVM, 4-space indent, 110 columns).
  - Run `python3 tools/gen_cjk_font.py --check-tables`, `python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py`, and `pio run -e lilygo-t-deck -e m5stack-cplus1_1` before reporting completion; disclose honestly any check that could not be executed.
  - Branch off `development` and target all PRs to `development` (maintainer handles `development` → `main`) with atomic Conventional Commits; comply with [SECURITY.md](./SECURITY.md).
