# AGENTS_ZH.md — 哪吒网络安全 (Nezha Cyber) AI 编程助手规范

> **语言 / Language**: [English](./AGENTS.md) | **简体中文**  
> 本文件与 [`AGENTS.md`](./AGENTS.md) 共同构成 **哪吒网络安全 (Nezha Cyber)** 项目的 AI 协作唯一权威规范（Single Source of Truth），适用于 Claude Code、AWS Kiro、Cursor、Trae、DeepSeek（Cline / Roo Code / Continue / Aider）、GitHub Copilot、Windsurf、OpenAI Codex、Gemini CLI 等全部 AI 编程工具。

---

## 1. 项目定位与版权归属

- **项目名称**：**哪吒网络安全 (Nezha Cyber)** —— 基于 ESP32 / ESP32-S3 / ESP32-C5 的独立开源便携式无线安全研究、射频（Sub-GHz）、红外、RFID 与硬件调试固件。
- **作者与维护者**：**钟智强 ([ctkqiang](https://github.com/ctkqiang))**。
- **开源协议**：**GNU AGPL-3.0**（[LICENSE](./LICENSE)）。本项目分支自 `pr3y/Bruce`，**必须**完整保留上游 Git 历史、版权声明与 [THIRD_PARTY.md](./THIRD_PARTY.md) 第三方致谢，严禁篡改或删除许可信息。
- **品牌与命名边界**：
  - 所有**面向用户的界面文案**、菜单、开机动画、WebUI、默认热点（`NezhaNet` / `nezhanet`）、WebUI 登录（`admin` / `nezha`）、二维码及发布固件文件名（`NezhaCyber-<env>.bin`），均须统一为 **哪吒网络安全 (Nezha Cyber)** 与 **哪吒魔童** 视觉体系。
  - **严禁**重新引入已移除的旧版用户界面品牌 `Brucegotchi`、`PwnGrid`、`Megalodon`（已由原创持久化 **哪吒宠物 / Nezha Pet** `src/modules/others/nezha_pet.*` 取代）。
  - **禁止**在未明确要求的情况下批量重命名历史内部 C++ 标识符（如 `BruceConfig`、`bruceConfig`、`bruce_i18n`、`BRUCE_COMPACT_CJK_FONT`、`BRUCE_VERSION`）或修改 `/bruce.conf` 配置文件路径，以免破坏配置兼容性与无意义放大 Diff。

---

## 2. 零 AI 垃圾代码红线（Anti-Slop 原则）

贡献者对其提交的每一行 AI 辅助代码承担 **100% 责任**。所有 AI 助手必须严格遵守：

1. **先读源码，再下笔**：严禁凭空臆造头文件路径、宏定义、GPIO 引脚、开发板环境名或函数签名。修改前必须核对 `src/`、`include/`、`boards/` 与 `platformio.ini` 中的真实实现。
2. **最小化、精准改动**：仅修改与当前任务直接相关的文件与代码行。严禁顺手重构无关模块、无故格式化未改动文件、随意调整头文件顺序，或堆砌过度设计的抽象封装层。
3. **杜绝占位符与空壳功能**：严禁提交 `// TODO: implement`、伪造的模拟扫描/射频数据、空回调按钮或未接通的菜单入口。每一个新增界面或选项都必须在真机上端到端可用。
4. **禁止敷衍式绕过错误**：严禁通过关闭 `-Werror=odr`、全局屏蔽编译警告、静默阉割 4 MB 板卡现有功能，或在未评估 Flash 占用的前提下向 `platformio.ini` 塞入外部依赖来糊弄编译。
5. **只写高信息量注释**：禁止废话注释（如 `// 循环遍历数组`）。仅在涉及硬件约束、SPI 总线隔离、DMA 内存阈值、Flash 分区预算或协议细节等非显而易见逻辑处添加精炼注释。
6. **禁止污染仓库**：严禁提交构建产物或自动生成文件（`NezhaCyber-*.bin`、`.pio/`、`__pycache__/`、`*.pyc`、`include/webFiles.h`、`include/current_year.h`、`compile_commands.json`）。
7. **如实汇报验证结果**：必须明确说明**实际执行了哪些**编译/脚本检查、哪些因环境或缺少硬件而未执行，严禁虚报“已通过编译”或“已在真机验证”。

---

## 3. 系统分层架构与启动模型

### 3.1 分层依赖架构
```text
┌─────────────────────────────────────────────────────────────────────────┐
│ 交互与入口层（Presentation & Entry）                                      │
│ MainMenu（7 套布局）· src/core/menu_items/* · serial_commands/* · WebUI  │
├─────────────────────────────────────────────────────────────────────────┤
│ 功能模块层（Feature Modules）                                            │
│ src/modules/{wifi, ble, rf, ir, rfid, lora, wire, others/*}             │
├─────────────────────────────────────────────────────────────────────────┤
│ 核心系统服务层（Core OS & Services）                                     │
│ display · theme · i18n · config · configPins · sd_functions · radio_mem │
│ location_state · scrollableTextArea · ram_profile                       │
├─────────────────────────────────────────────────────────────────────────┤
│ 总线仲裁与硬件抽象层（Bus Arbitration & Hardware HAL）                    │
│ bus_HAL（I2CLease / acquireSPIBus）· boards/<board>/interface.cc · HAL  │
└─────────────────────────────────────────────────────────────────────────┘
```
- **严格单向依赖**：仅允许上层调用下层，严禁将重型功能模块头文件反向引入全局公共头文件（`include/globals.h`、`src/core/radio_mem.h`）。
- **GUI 与 CLI 共用底层逻辑**：协议解析、文件加载与编码实现统一放在 `src/modules/` 或 `src/core/`（如 `RFIDInterface::loadFromFile()`、`buildNdefMessage()`），确保屏幕菜单（`src/core/menu_items/`）与串口 CLI（`src/core/serial_commands/` 基于 `SimpleCLI`）行为完全一致、不出现分叉。
- **板级 HAL 启动契约（`include/interface.h`、`boards/<board>/interface.cc`）**：
  1. `_setup_gpio()` —— 初始化 GPIO 与 `bruceConfigPins` 运行时默认引脚
  2. 初始化屏幕并播放 5 秒动态哪吒魔童开机动画
  3. `_pre_storage_gpio()` —— 首次 TFT 初始化之后、挂载文件系统之前的引脚准备
  4. 挂载存储（`setupLittleFS` / `setupSdCard`）并加载配置（`bruceConfig.fromFile()`、`bruceConfigPins.fromFile()`）
  5. `_post_setup_gpio()` —— 二阶段外设初始化（`setSysI2CBus()`、触屏、音频、IO 扩展芯片）
  6. 启动 FreeRTOS 输入任务 `taskInputHandler`，在递归互斥锁 `inputLock()` / `inputUnlock()` 保护下轮询 `InputHandler()`
  7. 电源与显示钩子：`getBattery()`、`isCharging()`、`_setBrightness(uint8_t)`、`powerOff()`、`goToDeepSleep()`、`checkReboot()`。

### 3.2 代码库目录速查

| 路径 | 职责说明 |
| :--- | :--- |
| `src/main.cc` | 启动流程、5 秒动态哪吒魔童开机动画、`taskInputHandler`、SPI 总线初始化、非 Lite 版 `16 KiB` `loopTask` 栈配置、主循环 |
| `src/core/` | 核心系统：显示（`display.*`）、主题（`theme.*`、`settingsColor.h`）、国际化（`i18n.*`、`lang_zh_cn.h`、自动生成的 `cjk_font_data.h`）、配置（`config.*`、`configPins.*`）、总线仲裁（`bus_HAL.*`）、存储（`sd_functions.*`）、启动器（`main_menu.*`）、射频内存守卫（`radio_mem.h`）、内存剖析（`ram_profile.*`）、定位状态（`location_state.*`）、滚动文本（`scrollableTextArea.*`） |
| `src/core/menu_items/` | 一级 `MenuItemInterface` 主菜单入口与功能模块调度 |
| `src/core/serial_commands/` | 交互式 `SimpleCLI` 串口命令处理器 |
| `src/core/tftLogger/` | 屏幕日志封装与 PROGMEM UTF-8 / CJK 点阵字库渲染器 |
| `src/core/wifi/` | Wi-Fi 管理、异步 WebUI 服务器、WireGuard 辅助模块 |
| `src/modules/` | 功能模块：`wifi/`、`ble/`、`rf/`、`ir/`、`rfid/`、`lora/`、`wire/`、`others/`（含 `nezha_pet.*`、`radar.*`、`flipper_tools.*`、`qrcode_menu.*` 等） |
| `boards/` | 各开发板 PlatformIO `.ini` 与 `interface.cc` 硬件抽象（主力验证：`boards/lilygo-t-deck/`、`boards/m5stack-cplus1_1/`） |
| `include/` | 全局公共头文件（`globals.h`、`interface.h`、`MenuItemInterface.h`、`DisplayDrivers_User_Setup.h`、`precompiler_flags.h`） |
| `partitions/` | 自定义 Flash 分区表 CSV（`custom_4Mb*.csv`、`custom_8Mb.csv`、`custom_16Mb.csv`） |
| `embedded_resources/` | WebUI 静态前端资源（编译时自动压缩生成被 git 忽略的 `include/webFiles.h`） |
| `scripts/build/` | PlatformIO 构建前后钩子脚本（`check_submodules.py`、`patch.py`、`gen_mqjs_headers.py`、`patch_library_conflicts.py`、`flto_prep.py`、`build.py`、`package_release.py`） |
| `tools/` | 主机端开发工具（`gen_cjk_font.py`、`gen_mfcodes.py`） |
| `.github/` | CI 编译检查（`ci.yml`）、自动发布流水线（`release.yml`）、手动构建、Issue 与 PR 模板 |

---

## 4. 核心数据结构与编码范式

### 4.1 核心数据结构与所有权约束

| 数据结构 | 所在位置 | 强制约束与使用规范 |
| :--- | :--- | :--- |
| `Option` + `loopOptions()` | `include/globals.h`、`src/core/display.h` | **关键防崩溃红线（防 UAF）**：子菜单中**必须**使用局部 `std::vector<Option> localOpts`（参考 `src/core/menu_items/RadarMenu.cc`），严禁在嵌套菜单复用全局 `options` 向量，否则子菜单清空全局 `options` 会直接销毁正在执行的父级 Lambda 导致崩溃！ |
| `MenuItemInterface` & `MainMenu` | `include/MenuItemInterface.h`、`src/core/main_menu.h` | 顶级应用继承 `MenuItemInterface`（实现 `optionsMenu()`、`drawIcon(float scale)`、`hasTheme()`、`themePath()`），在 `MainMenu` 注册；重型/可选应用须使用 `#if !defined(LITE_VERSION)` 隔离。`Option` 内置 `(x, y, w, h)` 命中框支持点选。 |
| `BruceConfig`（`: BruceTheme`） | `src/core/config.h` | 通过 `ArduinoJson` 持久化至 `/bruce.conf`。新增配置项必须补齐：安全默认值、`set*` 设置器、`validate*` 范围校验、JSON 读写及 `factoryReset()` 重置。 |
| `BruceConfigPins` | `src/core/configPins.h` | 结构化引脚组（`UARTPins`、`I2CPins`、`SPIPins`、`SpeakerPins`、`MicPins`），使用 `gpio_num_t`（默认 `GPIO_NUM_NC`）并内置 `fromJson()`/`toJson()`/`checkConflict()`。优先在 `_setup_gpio()` 赋默认值，减少僵化的编译期 `-D` 引脚宏。 |
| `I2CLease` & `I2CDeviceQuery` | `src/core/bus_HAL.h` | 仅可移动（move-only）的 RAII 租约（`i2cLeaseDevice`/`i2cLeaseBus`/`i2cLeasePins`）。持有 `TwoWire*` 的驱动**必须**将 `I2CLease` 作为成员变量，严禁对 `sys_i2c`（PMIC/触屏/RTC/IMU）直接调用 `end()`；M5Unified 板卡访问 `sys_i2c` 须配合 `lockSysI2CBus()` / `trylockSysI2CBus()` / `unlockSysI2CBus()`。 |
| `acquireSPIBus()` | `src/core/bus_HAL.h` | 仲裁 2 个硬件 SPI 控制器（`Display/SD` 优先于 `AUX_SPI`），必须妥善处理 `nullptr` 返回值（软件 SPI 降级或友好报错）。 |
| `RFIDInterface` | `src/modules/rfid/RFIDInterface.h` | 多态硬件驱动抽象基类（`enum ReturnCode`、定长 `Uid`/`NdefMessage`、虚函数 `begin`/`read`/`write`/`clone`/`emulate`/`loadFromFile`/`save`）。多芯片驱动族应遵循该多态接口设计。 |
| `BruceLocationState` & `RfLocationAnchor` | `src/core/location_state.h` | 定长 POD 记录（`uint8_t mac[6]`、`char name[20]`、`double lat/lon`、`int8_t rssi`），硬上限 `MAX_ANCHORS = 28`，以紧凑 JSON 存盘（`/location_state.json`）。 |
| `RawRecording` / `Protocol` | `src/modules/rf/structs.h` | 显式持有堆上 `rmt_symbol_word_t*` 缓冲，调用方在正常退出与异常分支均须完整释放内存。 |
| `FileList` + `FS &fs` | `src/core/sd_functions.h` | 文件操作统一接收 `FS &fs` / `FS *fs`（通过 `getFsStorage()` 获取），严禁硬编码 `SD`，确保无 SD 卡的 4 MB 板卡（如 `m5stack-cplus1_1`）可无缝回退至 `LittleFS`。 |
| `ScrollableTextArea` | `src/core/scrollableTextArea.h` | 日志与多行文本展示统一复用该组件，避免重复造轮子手写分页光标循环。 |
| `struct keyStroke` | `include/globals.h` | **严禁修改**（`// DO NOT CHANGE IT!!!!!` —— 输入与 HID ABI 契约）。 |

### 4.2 嵌入式数据结构选型守则
- **有界定长容器**：所有动态扫描/捕获列表（Wi-Fi AP、BLE 设备、RF 峰值、局域网主机、客户端）**必须**定义显式 `static constexpr size_t MAX_*` 上限，采用定长 POD 字段（`uint8_t mac[6]`、`char ssid[33]`、`int8_t rssi`）并提前 `reserve()`，禁止在扫描循环中无限增长 `std::vector<String>` 或 `std::map<String, String>`。
- **有序 `PROGMEM` 二分查找表**：静态只读数据集（翻译表、字库点阵、协议/OUI 表）统一使用有序 `static const ... PROGMEM` 数组配合 `O(log N)` 二分查找（同 `kZhCnTable`、`kCjkGlyphs` 模式），禁止放入堆上哈希表/红黑树浪费 DRAM。
- **持久化分层**：小型标量与宠物状态写入 ESP32 NVS（`Preferences`，如 `src/modules/others/nezha_pet.cc`）；结构化可迁移数据以有界 JSON 写入 `LittleFS` / `SD`（如 `/bruce.conf`、`/location_state.json`）。

### 4.3 标准 C++ 功能模块编码骨架（四阶段生命周期）
- **头文件与源文件隔离**：
  - `.h` 仅暴露最小公共入口与必要类型，重型硬件驱动头文件只在 `.cc` 中包含。
  - `.cc` 内的私有结构体、常量与辅助函数统一封装在匿名 `namespace { ... }`（或 `static`）中，减少 `-flto` 符号污染与 ODR 风险。
- **交互模块四阶段生命周期**：
  1. **前置守卫（Pre-flight Guard）**：检查射频 DMA（`radioHasMemForWifi()`/`radioHasMemForBle()`）、硬件总线（`i2cLeaseDevice()`/`acquireSPIBus()`）或存储（`getFsStorage()`）；失败则通过 `displayError(..., true)` 立即退出。
  2. **初始化与分配（Init）**：预分配有界缓冲，仅在 `psramFound()` 时创建大尺寸 PSRAM Sprite，并用 `check(EscPress)` / `check(SelPress)` 清除残留按键状态。
  3. **脏标记事件循环（Dirty-Flag Loop）**：`while (!returnToMenu)` → 检测到 `check(EscPress)` 立即干净退出 → 通过 `check(PrevPress/NextPress/UpPress/DownPress/SelPress)` 与 `touchPoint` 处理交互 → **仅在 `dirty` 为真或固定刷新周期内重绘**（杜绝无节制全屏 SPI 刷屏闪烁）→ 每次迭代调用 `vTaskDelay(pdMS_TO_TICKS(10..20))`。
  4. **确定性清理（Deterministic Teardown）**：调用 `deleteSprite()`、释放 `malloc`/`ps_malloc`/`rmt_symbol_word_t*` 内存、停止混杂模式回调/BLE 扫描/定时器/发射、让 RAII `I2CLease` 自动析构、若改过 `touchZoneOutsideFooterEnabled` 须恢复为 `true`，并重置屏幕状态。
- **零开销嵌入式 C++ 纪律**：
  - 固件代码中**禁用** C++ 异常（`try`/`catch`）、RTTI（`dynamic_cast`）、`<iostream>` 与 `std::regex`（会大幅膨胀 4 MB Flash 镜像）。
  - 统一使用 `bool`、`nullptr` 或作用域 `enum ReturnCode` 传递错误，并对一切报文长度、文件尺寸与指针分配执行严格边界校验。

---

## 5. ESP32 / ESP32-S3 嵌入式固件与内存工程规范

### 5.1 双档 Flash 预算（强制要求）
所有固件代码改动必须同时兼容两档主力目标：
- **16 MB 旗舰档（`lilygo-t-deck`、`lilygo-t-deck-pro`）**：ESP32-S3、16 MB Flash（`partitions/custom_16Mb.csv`）、8 MB OPI PSRAM、`320×240`、GT911 触屏 + 轨迹球 + 全键盘（`NO_TOUCH_FOOTER=1`）。
- **4 MB 紧凑档（`m5stack-cplus1_1`、`CYD-2432S028`）**：ESP32、4 MB Flash（`partitions/custom_4Mb_full.csv`，应用分区上限 `0x3E8000`）、**无 PSRAM**、`BRUCE_COMPACT_CJK_FONT=1`、`env_4mb`、`240×135`、三按键（`HAS_3_BUTTONS`）。
- **4 MB Lite / Launcher 档（`LAUNCHER_m5stack-cplus1_1`、`LAUNCHER_CYD-2432S028`）**：`LITE_VERSION=1`、`env_light`。

**预算守则**：
- 大体积视觉资源、大型查找表、重型编解码器及非核心扩展功能，必须使用 `#if !defined(LITE_VERSION)` 和/或 `#if !defined(BRUCE_COMPACT_CJK_FONT)` 条件编译隔离，确保 4 MB 目标稳定低于分区上限。
- 遵守 `env_4mb` 的 `lib_ignore`（`FastLED`、`ESP8266Audio`、`ESP8266SAM`）与受限红外协议宏（`_IR_ENABLE_DEFAULT_=false`）。
- 只读查找表必须使用 `PROGMEM` / `const` 留在 Flash 中，避免占用宝贵的内部 DRAM。

### 5.2 内部 DRAM 与连续 DMA 内存守卫（`src/core/radio_mem.h`）
- Wi-Fi、NimBLE 蓝牙栈与 SD/SPI DMA 缓冲区共用 **内部 DRAM（`MALLOC_CAP_DMA`）**。一旦最大连续 DMA 块耗尽，`esp_wifi_init` / `esp_bt_controller_init` 会陷入半初始化状态并在后续操作中触发 `LoadProhibited` / `abort()` 崩溃。
- 启动 Wi-Fi 或 BLE 前**必须**调用 `src/core/radio_mem.h` 中的 `radioHasMemForWifi()`、`radioHasMemForBle()` 与 `radioSafeBleInit()`，严禁绕过检查直接初始化射频驱动。
- 大尺寸非 DMA 帧缓冲/精灵图在 `psramFound()` 为真时优先放入 PSRAM，并为无 PSRAM 板卡提供安全降级或友好提示。
- 禁止在高频循环中反复拼接 Arduino `String` 导致堆碎片；优先使用栈上定长 `char buf[N]` 配合 `snprintf(buf, sizeof(buf), ...)`。
- 排查内部 DRAM 退化时，可使用 `RAM_LOG("stage")`（`src/core/ram_profile.h`）。

### 5.3 LTO、ODR、SPI 总线与 RTOS 纪律
- **严格遵守 ODR（`-flto -Werror=odr`）**：严禁同一 `struct` / `class` 因头文件宏条件不一致在不同 `.cc` 编译单元中产生不同内存布局。
- **严禁修改 `struct keyStroke`**（位于 `include/globals.h`，明确标注 `// DO NOT CHANGE IT!!!!!`）。
- **SPI 总线隔离（`src/main.cc`）**：严格遵守 `sdcardSPI(VSPI)` 与 `AUX_SPI(HSPI)` 分离及 `USE_HSPI_PORT` 路由逻辑，切勿破坏共享显示/触屏/SD 的 SPI 主机配置。
- **输入并发安全**：读写全局导航标志（`NextPress`、`PrevPress`、`UpPress`、`DownPress`、`SelPress`、`EscPress`、`touchPoint`）时遵循 `inputLock()` / `inputUnlock()` 约定。
- **看门狗与退出响应**：任何 `while` 阻塞循环中都必须包含 `vTaskDelay(pdMS_TO_TICKS(...))` / `yield()`，并提供明确的用户退出路径（`EscPress` / 返回键）。

---

## 6. UI、哪吒魔童主题与双形态交互规范

- **优先使用主题配色辅助函数**：统一调用 `src/core/display.h` 提供的 `uiSurfaceColor()`、`uiSurfaceHiColor()`、`uiBorderDimColor()`、`uiTextBrightColor()`、`uiTextMutedColor()`、`uiTextOnAccentColor()`、`blendColors()`、`uiDrawText16()`、`uiMeasureText16()`、`uiTruncateText16()`、`drawNezhaCrest()`、`drawNezhaRibbonRule()`、`buildHeatPalette()`；避免在通用控件中硬编码杂乱的 `TFT_*` RGB 常量。
- **双分辨率与双输入适配**：跨板卡功能必须同时良好适配：
  - `320×240` 触屏 + 轨迹球 + 全键盘（`lilygo-t-deck`）
  - `240×135` 三按键紧凑屏（`m5stack-cplus1_1`）
- **动态布局计算**：基于 `tftWidth`、`tftHeight`、`LW`、`LH` 与 `STATUS_BAR_HEIGHT` 计算布局，禁止写死在大屏有效但会在 `240×135` 裁切的魔数坐标。
- **UTF-8 / 中文安全截断**：使用 `bruce_i18n::utf8PixelWidth()`、`utf8TruncateToWidth()`、`utf8WrapToWidth()`、`utf8SafeCutBytes()` 测量与截断文本，杜绝多字节 UTF-8 中文乱码或越界。

---

## 7. 中英双语国际化（i18n）与 PROGMEM 点阵字库流水线

- 固件默认语言为 **简体中文（`zh-CN`）**，并支持在设置中即时切换至 **英文（`en`）**（`src/core/i18n.cc`）。
- C++ 源码中的界面文本须编写清晰的英文键，并通过 `bruce_i18n::tr(...)`（或内置翻译的核心菜单/弹窗接口）输出。
- 新增或修改中文翻译时，编辑 `src/core/lang_zh_cn.h`，且所有条目**必须严格按 ASCII 字节序（`strcmp` 升序）排列**（底层 `lookupExactZh()` 依赖 `O(log N)` 二分查找）。
- **严禁手改 `src/core/cjk_font_data.h`** —— 该文件由 `tools/gen_cjk_font.py` 自动生成（需要 Python 3、`Pillow` 及系统中文字体或 `BRUCE_CJK_FONT` 环境变量）。
- 每次修改 `src/core/lang_zh_cn.h` 或被扫描核心文件（`settings.cc`、`ConfigMenu.cc`、`i18n.cc`）中的中文字符后，**必须**执行：
  ```sh
  python3 tools/gen_cjk_font.py
  python3 tools/gen_cjk_font.py --check
  python3 tools/gen_cjk_font.py --check-tables
  ```

---

## 8. 代码风格与格式规范

- **C++ / C（`.cc` / `.h`）**：严格遵守 [`.clang-format`](./.clang-format)：
  - 基础风格：`LLVM`
  - 缩进：**4 空格**（`UseTab: Never`，`IndentWidth: 4`）
  - 单行上限：**110 列**（`ColumnLimit: 110`）
  - 花括号：`BreakBeforeBraces: Attach`，`AccessModifierOffset: -4`，`IndentCaseLabels: true`，`BinPackArguments: false`
- **Python（`scripts/build/*.py`、`tools/*.py`）**：兼容 Python 3.11+，并通过 `python3 -m py_compile` 语法检查。
- **内存与边界安全**：字符串格式化统一使用 `snprintf(..., sizeof(buf), ...)`；对所有无线报文、SD 文件解析与动态内存分配（`malloc` / `ps_malloc`）做完备边界与空指针校验。

---

## 9. 编译、验证与 CI 检查命令

提交或宣告任务完成前，必须执行对应验证：

```sh
# 1. 校验翻译表排序、CJK 字形覆盖率与 Python 语法（与 CI verify 任务一致）
python3 tools/gen_cjk_font.py --check-tables
python3 -m py_compile scripts/build/*.py tools/gen_cjk_font.py

# 2. 强制验证双档主力目标编译（16 MB 旗舰 + 4 MB 紧凑）
pio run -e lilygo-t-deck -e m5stack-cplus1_1

# 3. 可选：完整 CI 四板卡矩阵编译
pio run -e lilygo-t-deck -e m5stack-cplus1_1 -e m5stack-cardputer -e lilygo-t-embed-cc1101

# 4. 本地 Makefile 便捷构建
make build ENV=lilygo-t-deck
make build ENV=m5stack-cplus1_1
```

> 留意 `scripts/build/build.py` 输出的 `NEZHA: [...] XX.X%` 分区占用率，确认未超出目标 OTA/Factory 分区上限。

---

## 10. Git 工作流与原子提交规范

- **分支策略**：严禁直接向 `main` 提交，必须从 `main` 切出清晰命名的工作分支：
  - `feat/<short-name>` —— 新功能或硬件工具
  - `fix/<short-name>` —— 缺陷修复
  - `ui/<short-name>` —— UI / 哪吒魔童主题优化
  - `i18n/<locale>` —— 翻译与中文字库更新
  - `docs/<topic>` —— 文档与 AI 协作规范
  - `chore/<topic>` —— 构建、CI 或工程维护
- **原子化 Conventional Commits**：每项逻辑改动独立提交，采用标准前缀：`feat(scope): ...`、`fix(scope): ...`、`ui(scope): ...`、`perf(scope): ...`、`i18n(scope): ...`、`docs(scope): ...`、`chore(scope): ...`。
- **仓库安全**：严禁对 `main` 强制推送（`--force`），严禁提交密钥/令牌/个人 Wi-Fi 密码，严禁篡改上游提交历史。

---

## 11. 安全合规与合法授权红线

- 本固件的所有无线、射频、BLE、红外、RFID 与网络审计功能**仅限用于合法授权的安全研究、教学实验、硬件调试与个人实验室测试**（详见 [SECURITY.md](./SECURITY.md)）。
- 严禁在仓库中硬编码真实凭据、API Key（`wigleBasicToken`、`wdgwarsApiKey`）或未授权目标信息。
- 严禁削弱 WebUI 会话认证、植入隐蔽遥测/数据回传或添加破坏性变砖逻辑。

---

## 12. 已适配的 AI 工具矩阵与同步原则

`AGENTS.md` 与 `AGENTS_ZH.md` 为核心权威规则；各工具配置文件作为轻量化入口保持同步：

| AI 工具 / 生态 | 规则文件路径 |
| :--- | :--- |
| **通用标准（Codex / OpenCode / Jules / Amp 等）** | `AGENTS.md`、`AGENTS_ZH.md` |
| **Claude Code** | `CLAUDE.md` |
| **Gemini CLI / Antigravity** | `GEMINI.md` |
| **AWS Kiro** | `.kiro/steering/product.md`、`tech.md`、`structure.md`、`data-and-coding-patterns.md` |
| **Cursor** | `.cursor/rules/00-core-standards.mdc`、`10-esp32-firmware-memory.mdc`、`15-architecture-data-structures.mdc`、`20-i18n-cjk-fonts.mdc` |
| **Trae IDE** | `.trae/rules/project_rules.md` |
| **GitHub Copilot** | `.github/copilot-instructions.md` |
| **Windsurf** | `.windsurf/rules/nezha-cyber.md` |
| **DeepSeek / Cline** | `.clinerules` |
| **DeepSeek / Roo Code** | `.roo/rules/nezha-cyber.md` |
| **DeepSeek / Continue** | `.continue/rules/nezha-cyber.md` |
| **DeepSeek / Aider** | `.aider.conf.yml` |

若核心工程规范发生变更，请同步更新 `AGENTS.md`、`AGENTS_ZH.md` 及对应工具适配器。
