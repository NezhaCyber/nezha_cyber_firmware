---
trigger: always_on
description: Core anti-slop, ESP32/ESP32-S3 Flash & DMA safety, UI, and i18n rules for Nezha Cyber firmware
---

# Windsurf Cascade Rules — Nezha Cyber (哪吒网络安全)

> Single Source of Truth: [`AGENTS.md`](../../AGENTS.md) (EN) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md) (简体中文).

- **Zero AI Code Slop**: Read actual files in `src/`, `include/`, `boards/`, and `platformio.ini` before editing. Keep diffs minimal and task-scoped; no stubs, no fake RF/Wi-Fi data, no unsolicited refactors, no generated/binary commits (`NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `include/webFiles.h`).
- **Branding**: User-facing strings/UI use **Nezha Cyber (哪吒网络安全)** / **哪吒魔童** (author **钟智强 / ctkqiang**, **GNU AGPL-3.0**). Preserve `THIRD_PARTY.md` and legacy internal C++ identifiers (`BruceConfig`, `bruce_i18n`, `/bruce.conf`).
- **Dual Flash Budget**: Support both `lilygo-t-deck` (ESP32-S3, 16 MB, PSRAM, `320×240`) and `m5stack-cplus1_1` (ESP32, 4 MB, no PSRAM, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` ceiling). Guard heavy features with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)` and keep read-only tables in `PROGMEM`.
- **DMA & Hardware Safety**: Always call `radioHasMemForWifi()` / `radioHasMemForBle()` / `radioSafeBleInit()` (`src/core/radio_mem.h`). Respect `-flto -Werror=odr`, SPI host isolation (`sdcardSPI` vs `AUX_SPI`), `inputLock()`/`inputUnlock()`, watchdog yields, and never modify `struct keyStroke` (`include/globals.h`).
- **Architecture & Data Structures**: Respect layering (`menu_items`/`serial_commands` → `src/modules/` → `src/core/` → `bus_HAL`/`boards/*/interface.cc`). Always use a **local** `std::vector<Option>` inside submenus (never global `options` in nested menus), RAII `I2CLease` & `acquireSPIBus()`, `FS &fs` (`getFsStorage()`), bounded `MAX_*` POD scan tables, sorted `PROGMEM` lookup tables, and the 4-phase lifecycle (Guard → Init → Dirty-Flag Loop → Deterministic Teardown). Avoid C++ exceptions, RTTI, `<iostream>`, and `std::regex`.
- **UI & i18n**: Use `src/core/display.h` theme helpers and `bruce_i18n::tr()` / UTF-8 width helpers. Keep `src/core/lang_zh_cn.h` strictly ASCII-sorted; never hand-edit `src/core/cjk_font_data.h` — run `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.
- **Style & Verification**: Match `.clang-format` (LLVM, 4 spaces, 110 cols). Verify with `python3 tools/gen_cjk_font.py --check-tables`, `python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py`, and `pio run -e lilygo-t-deck -e m5stack-cplus1_1`. Branch off `main` with atomic Conventional Commits and follow `SECURITY.md`.
