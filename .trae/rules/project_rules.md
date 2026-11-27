# Nezha Cyber (哪吒网络安全) — Trae Project Rules / 项目开发规范

> **权威规范 / Single Source of Truth**: [`AGENTS.md`](../../AGENTS.md) (English) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md) (简体中文)

## 1. Anti-Slop Rules / 零 AI 垃圾代码红线
- **Read First / 先读后改**: Always inspect `src/`, `include/`, `boards/`, and `platformio.ini` before editing. Never invent pins, macros, headers, or board environments.
- **Minimal Diff / 精准改动**: Touch only files needed for the task. No drive-by refactoring, no unsolicited formatting of untouched files, no speculative abstractions.
- **No Stubs / 杜绝空壳**: Never leave `// TODO: implement`, mock RF/Wi-Fi telemetry, or dead UI entries.
- **No Dirty Artifacts / 禁止提交构建产物**: Never commit `NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `include/webFiles.h`, or `include/current_year.h`.
- **Honest Checks / 如实汇报**: State clearly which build/test commands actually ran and passed.

## 2. Branding & Compatibility / 品牌规范与兼容性
- User-facing copy & visuals: **Nezha Cyber (哪吒网络安全)** / **哪吒魔童**, maintained by **钟智强 (ctkqiang)** under **GNU AGPL-3.0**. Keep `THIRD_PARTY.md` and upstream `pr3y/Bruce` attribution intact.
- Do **not** restore legacy user-facing `Brucegotchi`, `PwnGrid`, or `Megalodon` names (replaced by **Nezha Pet / 哪吒宠物**).
- Do **not** mass-rename legacy internal C++ symbols (`BruceConfig`, `bruceConfig`, `bruce_i18n`, `BRUCE_COMPACT_CJK_FONT`, `/bruce.conf`).

## 3. ESP32 / ESP32-S3 Flash & RAM Rules / 嵌入式内存与 Flash 规范
- **Dual Flash Tiers / 双档兼容**: Every change must build cleanly on both **16 MB PSRAM** (`lilygo-t-deck`, `320×240`) and **4 MB no-PSRAM** (`m5stack-cplus1_1`, `240×135`, `BRUCE_COMPACT_CJK_FONT=1`, `0x3E8000` app limit).
- **Guard Heavy Code**: Wrap heavy UI assets, large tables, and optional codecs with `#if !defined(LITE_VERSION)` and/or `#if !defined(BRUCE_COMPACT_CJK_FONT)`. Store constant tables in `PROGMEM`.
- **DMA Guard (`src/core/radio_mem.h`)**: Always call `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` before bringing up Wi-Fi/NimBLE.
- **LTO & Hardware Safety**: Respect `-flto -Werror=odr`, `sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)` separation, `inputLock()`/`inputUnlock()`, watchdog yields (`vTaskDelay`), and **never** modify `struct keyStroke` (`include/globals.h`).

## 4. UI & Bilingual i18n (`zh-CN` default + `en`) / 界面与中文字库
- Use theme helpers from `src/core/display.h` (`uiSurfaceColor()`, `uiBorderDimColor()`, `uiTextBrightColor()`, `uiTextOnAccentColor()`, `uiDrawText16()`) and support both touch+trackball (`320×240`) and 3-button (`240×135`) controls.
- Route user-facing strings through `bruce_i18n::tr()`. Keep `src/core/lang_zh_cn.h` strictly ASCII `strcmp`-sorted.
- **Never** hand-edit `src/core/cjk_font_data.h`; regenerate and verify via:
  ```sh
  python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check && python3 tools/gen_cjk_font.py --check-tables
  ```

## 5. Formatting, Build Verification & Git / 格式化、验证与提交
- C++ follows `.clang-format` (LLVM, 4 spaces, 110 columns, attached braces).
- Verify before finishing:
  ```sh
  python3 tools/gen_cjk_font.py --check-tables
  python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py
  pio run -e lilygo-t-deck -e m5stack-cplus1_1
  ```
- Work on a dedicated branch off `main` with atomic Conventional Commits. Comply with `SECURITY.md` (authorized research/lab testing only; no hardcoded secrets).
