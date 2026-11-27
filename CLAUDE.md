# CLAUDE.md — Claude Code Instructions for Nezha Cyber (哪吒网络安全)

> Read [`AGENTS.md`](./AGENTS.md) (English) and [`AGENTS_ZH.md`](./AGENTS_ZH.md) (简体中文) as the Single Source of Truth for this repository.  
> 请以 [`AGENTS.md`](./AGENTS.md) 与 [`AGENTS_ZH.md`](./AGENTS_ZH.md) 作为本仓库的唯一权威工程规范。

## Core Mandates / 核心强制规范

1. **Zero AI Code Slop / 严禁 AI 垃圾代码**
   - Always read target files (`src/`, `include/`, `boards/`, `platformio.ini`) before editing. Never invent pins, macros, headers, or board names.
   - Keep diffs strictly scoped to the task. Do not refactor unrelated modules, reformat untouched files, or ship stubs / `TODO`s / fake telemetry.
   - Never commit binaries or generated outputs (`NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `include/webFiles.h`, `include/current_year.h`).

2. **Project Branding & Compatibility / 品牌与兼容性**
   - User-facing copy & assets: **Nezha Cyber (哪吒网络安全)** / **哪吒魔童**, author **钟智强 (ctkqiang)**, license **GNU AGPL-3.0**. Do not reintroduce legacy `Brucegotchi`/`PwnGrid`/`Megalodon` user-facing branding.
   - Do **not** mass-rename legacy internal C++ identifiers (`BruceConfig`, `bruceConfig`, `bruce_i18n`, `BRUCE_COMPACT_CJK_FONT`, `/bruce.conf`).

3. **ESP32 / ESP32-S3 Flash & Internal DRAM Discipline / 嵌入式内存与 Flash 红线**
   - Support **both** 16 MB PSRAM (`lilygo-t-deck`, `320×240`, touch+trackball+keyboard) and 4 MB no-PSRAM (`m5stack-cplus1_1`, `240×135`, 3-button, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` app limit).
   - Guard heavy assets/tables with `#if !defined(LITE_VERSION)` / `#if !defined(BRUCE_COMPACT_CJK_FONT)` and store read-only tables in `PROGMEM`.
   - Always gate Wi-Fi/NimBLE startup via `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` (`src/core/radio_mem.h`).
   - Respect `-flto -Werror=odr`, `sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)`, `inputLock()`/`inputUnlock()`, and never modify `struct keyStroke` (`include/globals.h`).

4. **UI & Bilingual i18n (`zh-CN` default + `en`) / 主题 UI 与双语字库**
   - Use theme helpers in `src/core/display.h` (`uiSurfaceColor()`, `uiBorderDimColor()`, `uiTextBrightColor()`, `uiTextOnAccentColor()`, `uiDrawText16()`) and `bruce_i18n::tr()` / UTF-8 width helpers.
   - Keep `src/core/lang_zh_cn.h` strictly ASCII `strcmp`-sorted. Never hand-edit `src/core/cjk_font_data.h`; regenerate with `python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check`.

5. **Formatting, Verification & Git / 格式化、编译验证与提交**
   - Match `.clang-format` (LLVM, 4 spaces, 110 cols, attached braces).
   - Verify before finishing:
     ```sh
     python3 tools/gen_cjk_font.py --check-tables
     python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py
     pio run -e lilygo-t-deck -e m5stack-cplus1_1
     ```
   - Never commit directly to `main`. Use atomic Conventional Commits on a feature branch. Report honestly which checks ran.
   - Strictly follow [SECURITY.md](./SECURITY.md) (authorized research/testing only; no hardcoded secrets).
