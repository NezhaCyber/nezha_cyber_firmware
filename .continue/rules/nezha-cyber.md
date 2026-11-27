---
name: Nezha Cyber Firmware Standards
description: Anti-slop, ESP32/ESP32-S3 memory/flash, UI, and i18n rules for Nezha Cyber (works with DeepSeek, Claude, GPT, Qwen, etc.)
alwaysApply: true
---

# Continue Rules — Nezha Cyber (哪吒网络安全)

> Canonical Rules / 权威规范: `AGENTS.md` (EN) | `AGENTS_ZH.md` (简体中文).

- **Zero AI Code Slop**: Inspect existing files in `src/`, `include/`, `boards/`, and `platformio.ini` before writing code. Keep edits minimal and scoped; never emit `TODO` stubs, mocked hardware data, or unrelated refactors.
- **Branding**: User-facing strings use **Nezha Cyber (哪吒网络安全)** / **哪吒魔童** (author **钟智强 / ctkqiang**, **GNU AGPL-3.0**). Keep `THIRD_PARTY.md` and legacy internal C++ identifiers (`BruceConfig`, `bruce_i18n`, `/bruce.conf`) untouched.
- **Dual Flash & DMA Safety**: Keep both `lilygo-t-deck` (16 MB ESP32-S3, PSRAM, `320×240`) and `m5stack-cplus1_1` (4 MB ESP32, no PSRAM, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` ceiling) building cleanly. Guard heavy features with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)` and gate radio init via `src/core/radio_mem.h` (`radioHasMemForWifi`, `radioHasMemForBle`, `radioSafeBleInit`).
- **Hardware Invariants**: Respect `-flto -Werror=odr`, `sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)`, `inputLock()`/`inputUnlock()`, watchdog yields, and never modify `struct keyStroke` (`include/globals.h`).
- **UI & i18n**: Use `src/core/display.h` theme helpers and `bruce_i18n::tr()`. Keep `src/core/lang_zh_cn.h` strictly ASCII `strcmp`-sorted; never hand-edit `src/core/cjk_font_data.h` — regenerate with `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.
- **Style & Checks**: Follow `.clang-format` (LLVM, 4 spaces, 110 cols). Verify with `python3 tools/gen_cjk_font.py --check-tables`, `python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py`, and `pio run -e lilygo-t-deck -e m5stack-cplus1_1`. Follow `SECURITY.md`.
