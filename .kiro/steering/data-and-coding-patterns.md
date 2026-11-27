---
inclusion: fileMatch
fileMatchPattern: "src/**/*.{cc,h}"
---

# Data Structures & Coding Patterns — Nezha Cyber Firmware

> Applies when editing C/C++ firmware under `src/`. Canonical spec: [`AGENTS.md`](../../AGENTS.md) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md).

## 1. Core Data Structures & Ownership Invariants / 核心数据结构规范

| Data Structure | File | Engineering Invariant |
| :--- | :--- | :--- |
| `Option` + `loopOptions()` | `include/globals.h`, `src/core/display.h` | **CRITICAL**: Always construct a **local** `std::vector<Option> localOpts` inside submenus (see `RadarMenu.cc`). Never reuse the global `options` vector for nested menus — a child menu clearing global `options` causes use-after-free on the executing lambda! |
| `MenuItemInterface` & `MainMenu` | `include/MenuItemInterface.h`, `src/core/main_menu.h` | Top-level apps inherit `MenuItemInterface` (`optionsMenu`, `drawIcon`, `hasTheme`, `themePath`). Guard optional/heavy apps with `#if !defined(LITE_VERSION)`. |
| `BruceConfig` (`: BruceTheme`) | `src/core/config.h` | Persists to `/bruce.conf` via `ArduinoJson`. Every new setting needs: default value, setter, validator (`validate*`), JSON load/save, and `factoryReset()` coverage. |
| `BruceConfigPins` | `src/core/configPins.h` | Structured pin structs (`UARTPins`, `I2CPins`, `SPIPins`, `SpeakerPins`, `MicPins`) using `gpio_num_t` defaulted to `GPIO_NUM_NC` with `fromJson()`/`toJson()`/`checkConflict()`. Prefer runtime defaults in `_setup_gpio()` over inflexible `-D` pin macros. |
| `I2CLease` & `I2CDeviceQuery` | `src/core/bus_HAL.h` | Move-only RAII lease (`i2cLeaseDevice`/`i2cLeaseBus`/`i2cLeasePins`). Drivers retaining a `TwoWire*` **must** hold `I2CLease` as a member so user-bus refcounting and `sys_i2c` clock/timeout restoration happen automatically. |
| `RFIDInterface` | `src/modules/rfid/RFIDInterface.h` | Polymorphic driver base (`enum ReturnCode`, fixed `Uid`/`NdefMessage`, virtual `begin/read/write/clone/emulate/loadFromFile/save`). Keep shared parsers on the base/module layer so GUI & CLI stay identical. |
| `BruceLocationState` | `src/core/location_state.h` | Bounded fixed-field records (`RfLocationAnchor` with `uint8_t mac[6]`, `char name[20]`, capped at `MAX_ANCHORS = 28`) persisted as compact JSON (`/location_state.json`). |
| `RawRecording` / `Protocol` | `src/modules/rf/structs.h` | Explicit ownership of `rmt_symbol_word_t*` buffers; callers **must** free allocated RMT symbol arrays on every exit/error path. |
| `FileList` + `FS &fs` | `src/core/sd_functions.h` | Always accept `FS &fs` / `FS *fs` (`getFsStorage()`) instead of hardcoding `SD`, ensuring SD-less boards (`m5stack-cplus1_1`) work seamlessly on `LittleFS`. |
| `ScrollableTextArea` | `src/core/scrollableTextArea.h` | Reuse for scrollable logs/text viewers instead of re-implementing manual paging loops. |
| `struct keyStroke` | `include/globals.h` | **DO NOT MODIFY** (ABI/input contract). |

## 2. Memory-Safe Data Structure Selection / 嵌入式数据结构选型
- **Bounded Containers**: Every scan result list (Wi-Fi APs, BLE devices, RF peaks, clients, hosts) **must** enforce a hard `static constexpr size_t MAX_*` cap and use fixed-width POD fields (`uint8_t mac[6]`, `char ssid[33]`, `int8_t rssi`) instead of unbounded `std::vector<String>` or `std::map<String, ...>`.
- **Flash Lookup Tables (`PROGMEM`)**: Store static protocol/OUI/i18n/font tables as sorted `static const ... PROGMEM` arrays queried via `O(log N)` binary search (pattern used by `kZhCnTable` and `kCjkGlyphs`) rather than heap-allocated maps.
- **Persistent State**: Small scalar/companion stats belong in ESP32 NVS (`Preferences`, e.g., `nezha_pet.cc`); structured portable data belongs in bounded JSON on `LittleFS`/`SD` (e.g., `/bruce.conf`, `/location_state.json`).

## 3. Standard Feature Module Coding Structure / 标准模块编码骨架
1. **File Layout**:
   - `src/modules/<domain>/<feature>.h` — Minimal public entry points & types; avoid pulling heavy driver headers into shared core headers.
   - `src/modules/<domain>/<feature>.cc` — Encapsulate internal structs, constants, and helpers in an anonymous `namespace { ... }` (or `static`) to keep `-flto` symbol visibility clean.
2. **4-Phase Interactive Lifecycle**:
   - **Phase 1 — Pre-flight Guard**: Check radio DMA (`radioHasMemForWifi()`/`radioHasMemForBle()`), hardware bus (`i2cLeaseDevice()`/`acquireSPIBus()`), or storage (`getFsStorage()`). On failure, show `displayError(..., true)` and return immediately.
   - **Phase 2 — Init & Allocate**: Allocate optional PSRAM sprite (`psramFound()`), reserve bounded buffers, clear stale input flags (`check(EscPress)`, `check(SelPress)`).
   - **Phase 3 — Dirty-Flag Loop**: Run `while (!returnToMenu)` → exit cleanly on `check(EscPress)` → process `check(PrevPress/NextPress/UpPress/DownPress/SelPress)` and `touchPoint` → redraw only when `dirty` or at bounded frame cadence using `display.h` theme helpers and `tftWidth`/`tftHeight` → `vTaskDelay(pdMS_TO_TICKS(10..20))`.
   - **Phase 4 — Deterministic Teardown**: Release sprites (`deleteSprite()`), free heap/RMT buffers, stop Wi-Fi promiscuous/BLE scan/TX timers, let `I2CLease` destruct, restore `touchZoneOutsideFooterEnabled = true` if altered, and leave the display clean.
3. **Error Handling**: Return `bool` or scoped `enum ReturnCode` (no C++ `try`/`catch` exceptions, no `dynamic_cast`/RTTI, no `<iostream>`, no `std::regex`). Validate all packet lengths, file sizes, and `malloc`/`ps_malloc` return pointers.
