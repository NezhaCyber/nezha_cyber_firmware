# AGENTS.md — AI Coding Agent Rules for Nezha Cyber (哪吒网络安全)

> **Language / 语言**: **English** | [简体中文](./AGENTS_ZH.md)  
> **Single Source of Truth (SSOT)** for all AI coding assistants and autonomous agents working on this repository (Claude Code, AWS Kiro, Cursor, Trae, DeepSeek via Cline/Roo/Continue/Aider, GitHub Copilot, Windsurf, OpenAI Codex, Gemini CLI, etc.).

---

## 1. Project Identity & Ownership / 项目定位与版权归属

- **Project**: **Nezha Cyber (哪吒网络安全)** — standalone ESP32 / ESP32-S3 / ESP32-C5 portable wireless security, RF, IR, RFID, and hardware utility firmware.
- **Author & Maintainer**: **钟智强 ([ctkqiang](https://github.com/ctkqiang))**.
- **License**: **GNU AGPL-3.0** ([LICENSE](./LICENSE)). Upstream lineage from `pr3y/Bruce` and third-party notices in [THIRD_PARTY.md](./THIRD_PARTY.md) **must** remain intact.
- **Branding Rules**:
  - All **user-facing** copy, menus, splash screens, WebUI, default AP (`NezhaNet` / `nezhanet`), WebUI login (`admin` / `nezha`), QR codes, and release binaries (`NezhaCyber-<env>.bin`) must use **Nezha Cyber (哪吒网络安全)** and the **哪吒魔童** visual identity.
  - **Do not** reintroduce legacy user-facing `Brucegotchi`, `PwnGrid`, or `Megalodon` branding (replaced by the original **Nezha Pet / 哪吒宠物** in `src/modules/others/nezha_pet.*`).
  - **Do not** mass-rename legacy internal C++ identifiers (`BruceConfig`, `bruceConfig`, `bruce_i18n`, `BRUCE_COMPACT_CJK_FONT`, `BRUCE_VERSION`) or change `/bruce.conf` without an explicit task requirement — keeping internal symbols stable avoids breaking config compatibility and diffs.

---

## 2. Anti-Slop Mandate (Zero AI Code Slop) / 严禁 AI 垃圾代码

Contributors using AI tools are **100% responsible** for every line they submit. Agents **must** follow these strict engineering guardrails:

1. **Read Before Writing**: Never guess file paths, header names, board flags, GPIO pins, or function signatures. Inspect the actual files in `src/`, `include/`, `boards/`, and `platformio.ini` first.
2. **Minimal, Scoped Diffs**: Modify only what the task requires. Do **not** drive-by refactor unrelated modules, reformat untouched files, reorder includes arbitrarily, or introduce speculative abstractions / wrapper layers.
3. **No Stubs, Fakes, or Dead UI**: Never ship `// TODO: implement`, placeholder telemetry, mock RF/Wi-Fi results, or menu items wired to empty callbacks. Every UI entry must execute real, end-to-end functionality.
4. **No Silent Shortcut Hacks**: Never silence compiler or linker errors by removing `-Werror=odr`, suppressing warnings globally, deleting working features from 4 MB boards silently, or adding unreviewed external dependencies to `platformio.ini`.
5. **Signal-Only Comments**: Never narrate obvious C++ syntax (`// loop over items`). Comment **only** non-obvious hardware invariants, DMA/SPI constraints, flash budget guards, or protocol quirks.
6. **Clean Working Tree**: Never commit build artifacts or generated files (`NezhaCyber-*.bin`, `.pio/`, `__pycache__/`, `*.pyc`, `include/webFiles.h`, `include/current_year.h`, `compile_commands.json`).
7. **Honest Verification Reporting**: State **explicitly** which builds, scripts, and hardware tests were actually executed and which were not. Never claim a build or board test passed without running it.

---

## 3. System Architecture, Layers & Boot Model / 系统分层架构与启动模型

### 3.1 Layered Dependency Architecture
```text
┌─────────────────────────────────────────────────────────────────────────┐
│ Presentation & Entry Layer                                              │
│ MainMenu (7 styles) · src/core/menu_items/* · serial_commands/* · WebUI │
├─────────────────────────────────────────────────────────────────────────┤
│ Feature Modules Layer                                                   │
│ src/modules/{wifi, ble, rf, ir, rfid, lora, wire, others/*}             │
├─────────────────────────────────────────────────────────────────────────┤
│ Core OS & Services Layer                                                │
│ display · theme · i18n · config · configPins · sd_functions · radio_mem │
│ location_state · scrollableTextArea · ram_profile                       │
├─────────────────────────────────────────────────────────────────────────┤
│ Bus Arbitration & Hardware HAL Layer                                    │
│ bus_HAL (I2CLease / acquireSPIBus) · boards/<board>/interface.cc · HAL  │
└─────────────────────────────────────────────────────────────────────────┘
```
- **Strict Dependency Direction**: Upper layers call into lower layers. Never leak heavy feature-module headers into global headers (`include/globals.h`, `src/core/radio_mem.h`).
- **Unified GUI + CLI Execution**: Keep protocol parsers, file loaders, and encoders inside `src/modules/` or `src/core/` (e.g., `RFIDInterface::loadFromFile()`, `buildNdefMessage()`) so TFT menus (`src/core/menu_items/`) and Serial CLI (`src/core/serial_commands/` via `SimpleCLI`) share one implementation and never diverge.
- **Board HAL Boot Contract (`include/interface.h`, `boards/<board>/interface.cc`)**:
  1. `_setup_gpio()` — initial GPIO & `bruceConfigPins` runtime defaults
  2. Display init + 5s animated 哪吒魔童 splash
  3. `_pre_storage_gpio()` — board pin setup between first TFT init and filesystem mount
  4. Storage (`setupLittleFS` / `setupSdCard`) + config load (`bruceConfig.fromFile()`, `bruceConfigPins.fromFile()`)
  5. `_post_setup_gpio()` — secondary hardware setup (`setSysI2CBus()`, touch, audio, expanders)
  6. FreeRTOS input task `taskInputHandler` polling `InputHandler()` under recursive mutex `inputLock()` / `inputUnlock()`
  7. Power/display hooks: `getBattery()`, `isCharging()`, `_setBrightness(uint8_t)`, `powerOff()`, `goToDeepSleep()`, `checkReboot()`.

### 3.2 Repository Directory Map / 目录结构速查

| Path | Responsibility |
| :--- | :--- |
| `src/main.cc` | Boot flow, 5s animated 哪吒魔童 splash, `taskInputHandler`, SPI host init, `loopTask` stack size (`16 KiB` on non-Lite builds), main loop |
| `src/core/` | Core OS: display (`display.*`), themes (`theme.*`, `settingsColor.h`), i18n (`i18n.*`, `lang_zh_cn.h`, autogenerated `cjk_font_data.h`), config (`config.*`, `configPins.*`), bus arbitration (`bus_HAL.*`), storage (`sd_functions.*`), launcher (`main_menu.*`), radio memory guard (`radio_mem.h`), RAM profiler (`ram_profile.*`), location (`location_state.*`), text viewer (`scrollableTextArea.*`) |
| `src/core/menu_items/` | Top-level `MenuItemInterface` menu entries & module dispatchers |
| `src/core/serial_commands/` | Interactive `SimpleCLI` serial command handlers |
| `src/core/tftLogger/` | Display logger & PROGMEM CJK/UTF-8 bitmap renderer |
| `src/core/wifi/` | Wi-Fi manager, Async WebUI server, WireGuard helpers |
| `src/modules/` | Feature modules: `wifi/`, `ble/`, `rf/`, `ir/`, `rfid/`, `lora/`, `wire/`, `others/` (`nezha_pet.*`, `radar.*`, `flipper_tools.*`, `qrcode_menu.*`, etc.) |
| `boards/` | Per-board PlatformIO `.ini` configs and `interface.cc` HALs (`boards/lilygo-t-deck/`, `boards/m5stack-cplus1_1/`, etc.) |
| `include/` | Global headers (`globals.h`, `interface.h`, `MenuItemInterface.h`, `DisplayDrivers_User_Setup.h`, `precompiler_flags.h`) |
| `partitions/` | Custom Flash partition CSV tables (`custom_4Mb*.csv`, `custom_8Mb.csv`, `custom_16Mb.csv`) |
| `embedded_resources/` | WebUI static assets (auto-gzipped into gitignored `include/webFiles.h` at build time) |
| `scripts/build/` | PlatformIO pre/post SCons hooks (`check_submodules.py`, `patch.py`, `gen_mqjs_headers.py`, `patch_library_conflicts.py`, `flto_prep.py`, `build.py`, `package_release.py`) |
| `tools/` | Host developer tools (`gen_cjk_font.py`, `gen_mfcodes.py`) |
| `.github/` | CI (`ci.yml`), automated Releases (`release.yml`), manual builds, issue & PR templates |

---

## 4. Core Data Structures & Coding Structures / 核心数据结构与编码范式

### 4.1 Core Data Structures & Ownership Invariants

| Data Structure | Location | Mandatory Invariant / Usage Rule |
| :--- | :--- | :--- |
| `Option` + `loopOptions()` | `include/globals.h`, `src/core/display.h` | **CRITICAL (UAF Prevention)**: Always build a **local** `std::vector<Option> localOpts` inside submenus (see `src/core/menu_items/RadarMenu.cc`). Never reuse the legacy global `options` vector for nested menus — a child menu clearing global `options` destroys the currently executing parent lambda. |
| `MenuItemInterface` & `MainMenu` | `include/MenuItemInterface.h`, `src/core/main_menu.h` | Top-level apps inherit `MenuItemInterface` (`optionsMenu()`, `drawIcon(float scale)`, `hasTheme()`, `themePath()`), register in `MainMenu`, and guard optional/heavy apps with `#if !defined(LITE_VERSION)`. `Option` carries `(x, y, w, h)` hit-test rects for tap-to-select. |
| `BruceConfig` (`: BruceTheme`) | `src/core/config.h` | Persists to `/bruce.conf` via `ArduinoJson`. Every new setting requires: default value, setter (`set*`), range validator (`validate*`), JSON serialization/deserialization, and `factoryReset()` reset. |
| `BruceConfigPins` | `src/core/configPins.h` | Structured pin groups (`UARTPins`, `I2CPins`, `SPIPins`, `SpeakerPins`, `MicPins`) using `gpio_num_t` defaulted to `GPIO_NUM_NC` with `fromJson()`/`toJson()`/`checkConflict()`. Prefer runtime defaults in `_setup_gpio()` over rigid `-D` pin macros. |
| `I2CLease` & `I2CDeviceQuery` | `src/core/bus_HAL.h` | Move-only RAII lease (`i2cLeaseDevice`/`i2cLeaseBus`/`i2cLeasePins`). Drivers storing `TwoWire*` **must** own `I2CLease` as a member; never raw-`end()` `sys_i2c` (PMIC/touch/RTC/IMU). On M5Unified boards, guard `sys_i2c` access with `lockSysI2CBus()` / `trylockSysI2CBus()` / `unlockSysI2CBus()`. |
| `acquireSPIBus()` | `src/core/bus_HAL.h` | Arbitrates the 2 hardware SPI hosts (`Display/SD` > `AUX_SPI`). Always handle a `nullptr` return (software SPI fallback or clean error). |
| `RFIDInterface` | `src/modules/rfid/RFIDInterface.h` | Polymorphic hardware driver contract (`enum ReturnCode`, fixed `Uid`/`NdefMessage`, virtual `begin`/`read`/`write`/`clone`/`emulate`/`loadFromFile`/`save`). Follow this interface-polymorphism pattern for multi-chip driver families. |
| `BruceLocationState` & `RfLocationAnchor` | `src/core/location_state.h` | Fixed-field POD records (`uint8_t mac[6]`, `char name[20]`, `double lat/lon`, `int8_t rssi`) capped at `MAX_ANCHORS = 28` and stored as compact JSON (`/location_state.json`). |
| `RawRecording` / `Protocol` | `src/modules/rf/structs.h` | Owns heap-allocated `rmt_symbol_word_t*` buffers — callers must free every allocated buffer on both normal exit and error paths. |
| `FileList` + `FS &fs` | `src/core/sd_functions.h` | Always take `FS &fs` / `FS *fs` (via `getFsStorage()`) instead of hardcoding `SD` so SD-less 4 MB boards (`m5stack-cplus1_1`) work seamlessly on `LittleFS`. |
| `ScrollableTextArea` | `src/core/scrollableTextArea.h` | Reuse for scrollable logs, dumps, and viewers rather than writing ad-hoc cursor paging loops. |
| `struct keyStroke` | `include/globals.h` | **DO NOT MODIFY** (`// DO NOT CHANGE IT!!!!!` — input/HID ABI contract). |

### 4.2 Embedded Data Structure Selection Rules
- **Bounded Fixed-Field Containers**: Every dynamic scan/capture table (Wi-Fi APs, BLE peripherals, RF peaks, LAN hosts, clients) **must** enforce an explicit `static constexpr size_t MAX_*` cap and use fixed-size POD fields (`uint8_t mac[6]`, `char ssid[33]`, `int8_t rssi`) + `reserve()`. Avoid unbounded `std::vector<String>` or `std::map<String, String>` in scan loops.
- **Sorted `PROGMEM` Binary-Search Tables**: Keep static lookup datasets (translations, font bitmaps, protocol/OUI tables) as sorted `static const ... PROGMEM` arrays queried in `O(log N)` binary search (same pattern as `kZhCnTable` and `kCjkGlyphs`).
- **Persistence Split**: Store small scalar/companion state in ESP32 NVS (`Preferences`, e.g., `src/modules/others/nezha_pet.cc`); store structured user-editable files as bounded JSON on `LittleFS` / `SD` (e.g., `/bruce.conf`, `/location_state.json`).

### 4.3 Standard C++ Module Coding Structure & 4-Phase Lifecycle
- **Header/Source Separation**:
  - `.h` declares only the minimal public API and shared types — keep heavy hardware/driver includes inside the `.cc`.
  - `.cc` places module-private structs, constants, and helper functions inside an anonymous `namespace { ... }` (or `static`) for clean `-flto` optimization and zero symbol collisions.
- **4-Phase Interactive Module Lifecycle**:
  1. **Pre-flight Guard**: Verify radio DMA (`radioHasMemForWifi()`/`radioHasMemForBle()`), hardware bus (`i2cLeaseDevice()`/`acquireSPIBus()`), or filesystem (`getFsStorage()`). Fail fast with `displayError(..., true)`.
  2. **Init & Allocate**: Reserve bounded buffers, allocate optional PSRAM sprite only when `psramFound()`, clear stale inputs (`check(EscPress)`, `check(SelPress)`).
  3. **Dirty-Flag Event Loop**: Run `while (!returnToMenu)`: exit cleanly on `check(EscPress)`; consume navigation via `check(PrevPress/NextPress/UpPress/DownPress/SelPress)` and `touchPoint`; redraw **only when `dirty` or at bounded FPS** (avoid continuous full-screen SPI flicker); call `vTaskDelay(pdMS_TO_TICKS(10..20))` each tick.
  4. **Deterministic Teardown**: Call `deleteSprite()`, free `malloc`/`ps_malloc`/`rmt_symbol_word_t*` buffers, stop promiscuous callbacks/BLE scans/timers/TX, allow RAII `I2CLease` to release, restore `touchZoneOutsideFooterEnabled = true` if altered, and leave the screen clean.
- **Zero-Overhead Embedded C++ Discipline**:
  - Do **not** use C++ exceptions (`try`/`catch`), RTTI (`dynamic_cast`), `<iostream>`, or `std::regex` — they inflate 4 MB Flash binaries severely.
  - Propagate errors via `bool`, `nullptr`, or scoped `enum ReturnCode`, and always bounds-check packet/frame/SD parsers.

---

## 5. ESP32 / ESP32-S3 Firmware & Memory Engineering Rules / 嵌入式内存与 Flash 规范

### 5.1 Dual Flash-Tier Budget (Mandatory)
Every firmware change must remain compatible with **both** primary hardware tiers:
- **16 MB Flagship (`lilygo-t-deck`, `lilygo-t-deck-pro`)**: ESP32-S3, 16 MB Flash (`partitions/custom_16Mb.csv`), 8 MB OPI PSRAM, `320×240`, GT911 touch + trackball + QWERTY keyboard (`NO_TOUCH_FOOTER=1`).
- **4 MB Compact (`m5stack-cplus1_1`, `CYD-2432S028`)**: ESP32, 4 MB Flash (`partitions/custom_4Mb_full.csv`, app limit `0x3E8000`), **no PSRAM**, `BRUCE_COMPACT_CJK_FONT=1`, `env_4mb`, `240×135`, 3 buttons (`HAS_3_BUTTONS`).
- **4 MB Lite / Launcher (`LAUNCHER_m5stack-cplus1_1`, `LAUNCHER_CYD-2432S028`)**: `LITE_VERSION=1`, `env_light`.

**Guard Rules**:
- Guard heavy UI assets, large lookup tables, optional codecs, and non-essential features with `#if !defined(LITE_VERSION)` and/or `#if !defined(BRUCE_COMPACT_CJK_FONT)` so 4 MB targets stay safely below their partition ceiling.
- Respect `env_4mb` `lib_ignore` (`FastLED`, `ESP8266Audio`, `ESP8266SAM`) and restricted IR protocol macros (`_IR_ENABLE_DEFAULT_=false`).
- Keep read-only lookup tables in Flash via `PROGMEM` / `const`, never in mutable DRAM.

### 5.2 Internal DRAM & DMA Contiguous Block Safety (`src/core/radio_mem.h`)
- Wi-Fi, NimBLE, and SD/SPI DMA buffers share **internal DRAM (`MALLOC_CAP_DMA`)**. Exhausting the largest contiguous DMA block causes `esp_wifi_init` / `esp_bt_controller_init` crashes (`LoadProhibited` / `abort()`).
- **Always** gate radio bring-up with `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` from `src/core/radio_mem.h`. Never bypass these guards.
- Allocate large non-DMA buffers/sprites in PSRAM when `psramFound()` is true, with clean fallback or refusal on non-PSRAM boards.
- Avoid heap fragmentation in tight loops: prefer stack-allocated `char buf[N]` + `snprintf(buf, sizeof(buf), ...)` over repeated Arduino `String` concatenation.
- Use `RAM_LOG("stage")` (`src/core/ram_profile.h`) to diagnose internal DRAM regressions.

### 5.3 LTO, ODR, SPI Buses & RTOS Hygiene
- **Strict ODR (`-flto -Werror=odr`)**: Never let a `struct` or `class` have differing member layouts across `.cc` files due to inconsistent `#ifdef` macro visibility.
- **Never modify `struct keyStroke`** in `include/globals.h` (marked `// DO NOT CHANGE IT!!!!!`).
- **SPI Host Isolation (`src/main.cc`)**: Respect `sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)` and `USE_HSPI_PORT` routing; never silently rebind shared display/touch/SD SPI hosts.
- **Input Thread Safety**: Respect `inputLock()` / `inputUnlock()` around shared navigation flags (`NextPress`, `PrevPress`, `UpPress`, `DownPress`, `SelPress`, `EscPress`, `touchPoint`).
- **Watchdog & Responsiveness**: Never spin in blocking `while` loops without `vTaskDelay(pdMS_TO_TICKS(...))` / `yield()` and an explicit user exit path (`EscPress` / cancel).

---

## 6. UI, 哪吒魔童 Theme & Dual-Input Ergonomics / 界面主题与交互规范

- **Theme Helpers First**: Use `src/core/display.h` helpers (`uiSurfaceColor()`, `uiSurfaceHiColor()`, `uiBorderDimColor()`, `uiTextBrightColor()`, `uiTextMutedColor()`, `uiTextOnAccentColor()`, `blendColors()`, `uiDrawText16()`, `uiMeasureText16()`, `uiTruncateText16()`, `drawNezhaCrest()`, `drawNezhaRibbonRule()`, `buildHeatPalette()`). Do not hardcode random `TFT_*` RGB values for UI chrome.
- **Dual Screen & Input Support**: Every cross-board screen must remain usable on both:
  - `320×240` Touch + Trackball + Keyboard (`lilygo-t-deck`)
  - `240×135` 3-Button compact displays (`m5stack-cplus1_1`)
- **Responsive Layout**: Compute positions dynamically from `tftWidth`, `tftHeight`, `LW`, `LH`, and `STATUS_BAR_HEIGHT` — avoid magic coordinates that clip on `240×135`.
- **UTF-8 / CJK Safe Text**: Use `bruce_i18n::utf8PixelWidth()`, `utf8TruncateToWidth()`, `utf8WrapToWidth()`, and `utf8SafeCutBytes()` so multi-byte UTF-8 / Chinese glyphs never split mid-character or overflow containers.

---

## 7. Bilingual i18n & CJK Bitmap Font Pipeline / 国际化与中文字库规范

- Firmware defaults to **Simplified Chinese (`zh-CN`)** and switches dynamically to **English (`en`)** (`src/core/i18n.cc`).
- Write clean English source keys in C++ and route user-visible text through `bruce_i18n::tr(...)` (or core menu/dialog APIs that call `tr()`).
- Add corresponding Chinese translations to `src/core/lang_zh_cn.h` in **strict ASCII byte (`strcmp`) ascending order** (`O(log N)` binary search depends on this).
- **NEVER edit `src/core/cjk_font_data.h` manually** — it is autogenerated by `tools/gen_cjk_font.py` (requires Python 3, `Pillow`, and a system CJK font or `BRUCE_CJK_FONT` env var).
- After **any** edit to `src/core/lang_zh_cn.h` or CJK literals in scanned files (`settings.cc`, `ConfigMenu.cc`, `i18n.cc`), run:
  ```sh
  python3 tools/gen_cjk_font.py
  python3 tools/gen_cjk_font.py --check
  python3 tools/gen_cjk_font.py --check-tables
  ```

---

## 8. Code Style & Formatting / 代码风格规范

- **C++ / C (`.cc` / `.h`)**: Strictly follow [`.clang-format`](./.clang-format):
  - Style base: `LLVM`
  - Indentation: **4 spaces** (`UseTab: Never`, `IndentWidth: 4`)
  - Max line length: **110 columns** (`ColumnLimit: 110`)
  - Braces: `BreakBeforeBraces: Attach`, `AccessModifierOffset: -4`, `IndentCaseLabels: true`, `BinPackArguments: false`
- **Python (`scripts/build/*.py`, `tools/*.py`)**: Python 3.11+ compatible, clean type hints where appropriate, verified with `python3 -m py_compile`.
- **Safety**: Use `snprintf` with `sizeof(buf)`, bounds-check all packet/frame/SD parsers, and check pointer/allocation return values (`nullptr` checks on `malloc`/`ps_malloc`).

---

## 9. Build, Verification & CI Commands / 编译与验证清单

Run the relevant checks **before** declaring work done:

```sh
# 1. i18n table sort order, CJK glyph coverage & Python syntax (runs in CI verify job)
python3 tools/gen_cjk_font.py --check-tables
python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py

# 2. Mandatory dual-tier PlatformIO firmware build (16 MB flagship + 4 MB compact)
pio run -e lilygo-t-deck -e m5stack-cplus1_1

# 3. Optional full CI matrix (all 4 CI board targets)
pio run -e lilygo-t-deck -e m5stack-cplus1_1 -e m5stack-cardputer -e lilygo-t-embed-cc1101

# 4. Local Makefile developer helpers
make build ENV=lilygo-t-deck
make build ENV=m5stack-cplus1_1
```

> Confirm in `scripts/build/build.py` output (`NEZHA: [...] XX.X%`) that firmware usage stays safely within each target's OTA/factory partition limit.

---

## 10. Git Workflow & Commit Discipline / Git 分支与提交规范

- **Branching**: Always work on a dedicated branch off `main`:
  - `feat/<short-name>` — new features or hardware tools
  - `fix/<short-name>` — bug fixes
  - `ui/<short-name>` — UI / 哪吒魔童 theme polish
  - `i18n/<locale>` — translation & CJK font updates
  - `docs/<topic>` — documentation & AI rules
  - `chore/<topic>` — build, CI, or maintenance
- **Atomic Conventional Commits**: Keep each commit focused on one logical change using Conventional Commits:
  `feat(scope): ...`, `fix(scope): ...`, `ui(scope): ...`, `perf(scope): ...`, `i18n(scope): ...`, `docs(scope): ...`, `chore(scope): ...`.
- **Safety**: Never force-push `main`, never commit secrets/tokens/personal Wi-Fi credentials, and never rewrite upstream history.

---

## 11. Security & Authorized-Use Policy / 安全与合法合规红线

- All wireless, RF, BLE, IR, RFID, and network capabilities are strictly for **authorized security research, education, hardware debugging, and personal lab testing** ([SECURITY.md](./SECURITY.md)).
- Never hardcode real credentials, API keys (`wigleBasicToken`, `wdgwarsApiKey`), or private target data.
- Never weaken WebUI session authentication, introduce covert exfiltration/telemetry, or add destructive bricking routines.

---

## 12. Supported AI Tool Adapters / 已适配的 AI 工具与规则同步

`AGENTS.md` and [AGENTS_ZH.md](./AGENTS_ZH.md) are the canonical rules. Thin tool-specific adapters ensure consistent enforcement across contributor workflows:

| Tool / Ecosystem | Adapter File(s) |
| :--- | :--- |
| **Universal (Codex, OpenCode, Jules, Amp, etc.)** | `AGENTS.md`, `AGENTS_ZH.md` |
| **Claude Code** | `CLAUDE.md` |
| **Gemini CLI / Antigravity** | `GEMINI.md` |
| **AWS Kiro** | `.kiro/steering/product.md`, `tech.md`, `structure.md`, `data-and-coding-patterns.md` |
| **Cursor** | `.cursor/rules/00-core-standards.mdc`, `10-esp32-firmware-memory.mdc`, `15-architecture-data-structures.mdc`, `20-i18n-cjk-fonts.mdc` |
| **Trae IDE** | `.trae/rules/project_rules.md` |
| **GitHub Copilot** | `.github/copilot-instructions.md` |
| **Windsurf** | `.windsurf/rules/nezha-cyber.md` |
| **DeepSeek / Cline** | `.clinerules` |
| **DeepSeek / Roo Code** | `.roo/rules/nezha-cyber.md` |
| **DeepSeek / Continue** | `.continue/rules/nezha-cyber.md` |
| **DeepSeek / Aider** | `.aider.conf.yml` |

When updating core rules, keep `AGENTS.md`, `AGENTS_ZH.md`, and affected adapters synchronized.
