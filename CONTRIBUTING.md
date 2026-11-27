# Contributing to Nezha Cyber (哪吒网络安全)

Thank you for your interest in contributing to **Nezha Cyber (哪吒网络安全)**! We welcome bug reports, localization improvements, new board ports, hardware tools, and UI refinements from the open-source community.

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](./CODE_OF_CONDUCT.md) and our [Security & Legal Policy](./SECURITY.md).

## Development Workflow

1. **Fork & Branch**: Fork the repository and create a descriptive feature branch off `main`:
   - `feat/<short-name>` for new features
   - `fix/<short-name>` for bug fixes
   - `i18n/<locale>` for translation updates
   - `docs/<topic>` for documentation updates
2. **Keep Commits Atomic**: Use Conventional Commits (`feat(rf): ...`, `fix(ui): ...`, `i18n(ko): ...`, `docs: ...`).
3. **Build-Verify Both Flash Tiers**:
   Before opening a Pull Request, verify that **both** a 16 MB flagship target (`lilygo-t-deck`) and a 4 MB compact target (`m5stack-cplus1_1`) build cleanly:
   ```sh
   pio run -e lilygo-t-deck -e m5stack-cplus1_1
   ```
   > **4 MB Flash Budget Rule**: Heavy UI assets, large lookup tables, and extended locales should be guarded with `#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)` so 4 MB targets (`m5stack-cplus1_1`, `CYD-*`) stay under their `0x3E8000` factory/OTA limit.

## Adding or Updating Translations (i18n)

- Simplified Chinese (`zh-CN`, default) translations live in [`src/core/lang_zh_cn.h`](./src/core/lang_zh_cn.h); English (`en`) is built into source strings.
- The table **must remain strictly sorted in ASCII `strcmp` order** for O(log N) binary search.
- After editing translations, regenerate and verify the PROGMEM CJK bitmap font header:
  ```sh
  python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check
  ```

## UI & Brand Standards

- Use the theme-derived palette helpers (`uiSurfaceColor()`, `uiBorderDimColor()`, `uiTextBrightColor()`, `uiTextOnAccentColor()`, `uiDrawText16()`) rather than hardcoded RGB values where appropriate.
- Support both **touch + trackball/keyboard** (`320×240`) and **3-button compact** (`240×135`) interaction models for cross-board features.
- Keep user-facing branding aligned with **Nezha Cyber (哪吒网络安全)** and the **哪吒魔童** visual identity.

## AI-Assisted Contributions / AI 辅助贡献规范

AI coding tools (Claude Code, Cursor, Trae, DeepSeek via Cline/Roo/Continue/Aider, GitHub Copilot, Windsurf, Codex, Gemini CLI, etc.) are welcome **only** when contributors strictly follow our canonical agent rules in [`AGENTS.md`](./AGENTS.md) ([简体中文](./AGENTS_ZH.md)):

- You remain **100% responsible** for understanding, reviewing, and verifying every line you submit — unreviewed "AI code slop", stubbed `TODO`s, hallucinated pins/macros, or drive-by refactors will be rejected.
- Tool-specific rule files (`.cursor/rules/`, `.trae/rules/`, `CLAUDE.md`, `GEMINI.md`, `.github/copilot-instructions.md`, `.windsurf/rules/`, `.clinerules`, `.roo/rules/`, `.continue/rules/`, `.aider.conf.yml`) are maintained in-repo to automatically keep AI assistants aligned with `AGENTS.md`.

## Licensing

All contributions are licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)** — see [LICENSE](./LICENSE) and [THIRD_PARTY.md](./THIRD_PARTY.md).
