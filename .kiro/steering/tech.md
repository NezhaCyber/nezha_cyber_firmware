---
inclusion: always
---

# Tech Steering — Nezha Cyber (哪吒网络安全)

> Canonical specification / 权威规范: [`AGENTS.md`](../../AGENTS.md) (English) | [`AGENTS_ZH.md`](../../AGENTS_ZH.md) (简体中文)

## Platform, Toolchain & Compiler Flags / 平台与编译选项
- **Build System**: PlatformIO (`platformio.ini` + `boards/*/*.ini`) with `pioarduino/platform-espressif32` (Arduino-ESP32 3.3.x / ESP-IDF 5.x) and `Makefile` convenience targets.
- **Optimization & Linker Flags**: `-Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -Werror=odr`.
  - **Strict ODR**: Never allow differing `struct`/`class` layouts across translation units due to inconsistent `#ifdef` macro visibility.
  - **No C++ Exceptions / RTTI / `<iostream>` / `std::regex`**: Avoid them in firmware to protect the 4 MB Flash budget.
- **SCons Hooks (`scripts/build/`)**: `check_submodules.py`, `patch.py`, `pre_build_current_year.py`, `gen_mqjs_headers.py`, `patch_library_conflicts.py`, `flto_prep.py`, `build.py` (validates OTA partition size and emits `NezhaCyber-<env>.bin`).

## Memory, Flash & Radio Guardrails / 内存、Flash 与射频守卫
- **Flash Tier Guards**:
  - Guard heavy UI assets, large tables, optional codecs, and optional apps with `#if !defined(LITE_VERSION)` and/or `#if !defined(BRUCE_COMPACT_CJK_FONT)`.
  - Respect `env_4mb` `lib_ignore` (`FastLED`, `ESP8266Audio`, `ESP8266SAM`) and restricted IR macros (`_IR_ENABLE_DEFAULT_=false`).
  - Place read-only lookup tables in Flash (`PROGMEM` / `const`), never mutable DRAM.
- **Internal DRAM & DMA Contiguous Block Guard (`src/core/radio_mem.h`)**:
  - Wi-Fi, NimBLE, and SD/SPI DMA buffers share internal `MALLOC_CAP_DMA` memory.
  - **Always** call `radioHasMemForWifi()`, `radioHasMemForBle()`, and `radioSafeBleInit()` before bringing up radios.
  - Use `RAM_LOG("stage")` (`src/core/ram_profile.h`) to diagnose internal DRAM regressions.
- **PSRAM & Heap Hygiene**:
  - Allocate large non-DMA buffers/sprites in PSRAM when `psramFound()` is true, with clean non-PSRAM fallback and deterministic `deleteSprite()` / `free()` on exit.
  - Avoid Arduino `String` concatenation in hot loops; prefer stack `char buf[N]` + `snprintf(buf, sizeof(buf), ...)`.
- **RTOS & Stack**:
  - `loopTask` stack is `16 KiB` on non-Lite builds (`SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in `src/main.cc`).
  - Never block loops without `vTaskDelay(pdMS_TO_TICKS(...))` / `yield()` and an explicit `EscPress` exit path.

## Bus Arbitration & Hardware Safety (`src/core/bus_HAL.h`)
- **I2C**: Always acquire an RAII `I2CLease` via `i2cLeaseDevice()` / `i2cLeaseBus()` / `i2cLeasePins()`. Never raw-`end()` `sys_i2c` (PMIC/touch/RTC/IMU). On M5Unified boards, use `lockSysI2CBus()` / `trylockSysI2CBus()` / `unlockSysI2CBus()`.
- **SPI**: Respect `sdcardSPI(VSPI)` vs `AUX_SPI(HSPI)` and `USE_HSPI_PORT`. Acquire auxiliary SPI through `acquireSPIBus(sck, miso, mosi)` and handle `nullptr`.
- **Input Concurrency**: Never alter `struct keyStroke` (`include/globals.h`). Always consume input flags via `check(...)` (`inputLock()`/`inputUnlock()`), never `vTaskSuspend` the input task.

## Bilingual i18n & CJK Font Pipeline (`tools/gen_cjk_font.py`)
- Source strings are English keys passed through `bruce_i18n::tr(...)`.
- `src/core/lang_zh_cn.h` **must** remain strictly sorted in ascending ASCII `strcmp` order (`O(log N)` binary search).
- **Never** hand-edit `src/core/cjk_font_data.h`; regenerate and verify via:
  ```sh
  python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check && python3 tools/gen_cjk_font.py --check-tables
  ```

## Mandatory Verification Commands / 编译与验证命令
```sh
python3 tools/gen_cjk_font.py --check-tables
python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py
pio run -e lilygo-t-deck -e m5stack-cplus1_1
```
Always report honestly which commands were actually executed.
