# AGENTS_ZH.md — 哪吒网络安全 (Nezha Cyber) AI 编程助手规范

> **语言 / Language**: [English](./AGENTS.md) | **简体中文**  
> 本文件与 [`AGENTS.md`](./AGENTS.md) 共同构成 **哪吒网络安全 (Nezha Cyber)** 项目的 AI 协作唯一权威规范（Single Source of Truth），适用于 Claude Code、Cursor、Trae、DeepSeek（Cline / Roo Code / Continue / Aider）、GitHub Copilot、Windsurf、OpenAI Codex、Gemini CLI 等全部 AI 编程工具。

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

## 3. 代码库目录速查

| 路径 | 职责说明 |
| :--- | :--- |
| `src/main.cc` | 启动流程、5 秒动态哪吒魔童开机动画、`taskInputHandler`、SPI 总线初始化、非 Lite 版 `16 KiB` `loopTask` 栈配置、主循环 |
| `src/core/` | 核心系统：显示（`display.*`）、主题（`theme.*`、`settingsColor.h`）、国际化（`i18n.*`、`lang_zh_cn.h`、自动生成的 `cjk_font_data.h`）、配置（`config.*`、`configPins.*`）、启动器（`main_menu.*`）、射频内存守卫（`radio_mem.h`）、内存剖析（`ram_profile.*`） |
| `src/core/menu_items/` | 一级主菜单入口与功能模块调度 |
| `src/core/serial_commands/` | 交互式串口 CLI 命令处理器 |
| `src/core/tftLogger/` | 屏幕日志封装与 PROGMEM UTF-8 / CJK 点阵字库渲染器 |
| `src/core/wifi/` | Wi-Fi 管理、异步 WebUI 服务器、WireGuard 辅助模块 |
| `src/modules/` | 功能模块：`wifi/`、`ble/`、`rf/`、`ir/`、`rfid/`、`lora/`、`wire/`、`others/`（含 `nezha_pet.*`、`radar.*`、`flipper_tools.*`、`qrcode_menu.*` 等） |
| `boards/` | 各开发板 PlatformIO `.ini` 与 `interface.cc` 硬件抽象（主力验证：`boards/lilygo-t-deck/`、`boards/m5stack-cplus1_1/`） |
| `include/` | 全局公共头文件（`globals.h`、`DisplayDrivers_User_Setup.h`、`precompiler_flags.h`） |
| `partitions/` | 自定义 Flash 分区表 CSV（`custom_4Mb*.csv`、`custom_8Mb.csv`、`custom_16Mb.csv`） |
| `embedded_resources/` | WebUI 静态前端资源（编译时自动压缩生成被 git 忽略的 `include/webFiles.h`） |
| `scripts/build/` | PlatformIO 构建前后钩子脚本（`check_submodules.py`、`patch.py`、`gen_mqjs_headers.py`、`patch_library_conflicts.py`、`flto_prep.py`、`build.py`、`package_release.py`） |
| `tools/` | 主机端开发工具（`gen_cjk_font.py`、`gen_mfcodes.py`） |
| `.github/` | CI 编译检查（`ci.yml`）、自动发布流水线（`release.yml`）、手动构建、Issue 与 PR 模板 |

---

## 4. ESP32 / ESP32-S3 嵌入式固件与内存工程规范

### 4.1 双档 Flash 预算（强制要求）
所有固件代码改动必须同时兼容两档主力目标：
- **16 MB 旗舰档（`lilygo-t-deck`、`lilygo-t-deck-pro`）**：ESP32-S3、16 MB Flash（`partitions/custom_16Mb.csv`）、8 MB OPI PSRAM、`320×240`、GT911 触屏 + 轨迹球 + 全键盘（`NO_TOUCH_FOOTER=1`）。
- **4 MB 紧凑档（`m5stack-cplus1_1`、`CYD-2432S028`）**：ESP32、4 MB Flash（`partitions/custom_4Mb_full.csv`，应用分区上限 `0x3E8000`）、**无 PSRAM**、`BRUCE_COMPACT_CJK_FONT=1`、`env_4mb`、`240×135`、三按键（`HAS_3_BUTTONS`）。
- **4 MB Lite / Launcher 档（`LAUNCHER_m5stack-cplus1_1`、`LAUNCHER_CYD-2432S028`）**：`LITE_VERSION=1`、`env_light`。

**预算守则**：
- 大体积视觉资源、大型查找表、重型编解码器及非核心扩展功能，必须使用 `#if !defined(LITE_VERSION)` 和/或 `#if !defined(BRUCE_COMPACT_CJK_FONT)` 条件编译隔离，确保 4 MB 目标稳定低于分区上限。
- 遵守 `env_4mb` 的 `lib_ignore`（`FastLED`、`ESP8266Audio`、`ESP8266SAM`）与受限红外协议宏（`_IR_ENABLE_DEFAULT_=false`）。
- 只读查找表必须使用 `PROGMEM` / `const` 留在 Flash 中，避免占用宝贵的内部 DRAM。

### 4.2 内部 DRAM 与连续 DMA 内存守卫（`src/core/radio_mem.h`）
- Wi-Fi、NimBLE 蓝牙栈与 SD/SPI DMA 缓冲区共用 **内部 DRAM（`MALLOC_CAP_DMA`）**。一旦最大连续 DMA 块耗尽，`esp_wifi_init` / `esp_bt_controller_init` 会陷入半初始化状态并在后续操作中触发 `LoadProhibited` / `abort()` 崩溃。
- 启动 Wi-Fi 或 BLE 前**必须**调用 `src/core/radio_mem.h` 中的 `radioHasMemForWifi()`、`radioHasMemForBle()` 与 `radioSafeBleInit()`，严禁绕过检查直接初始化射频驱动。
- 大尺寸非 DMA 帧缓冲/精灵图在 `psramFound()` 为真时优先放入 PSRAM，并为无 PSRAM 板卡提供安全降级或友好提示。
- 禁止在高频循环中反复拼接 Arduino `String` 导致堆碎片；优先使用栈上定长 `char buf[N]` 配合 `snprintf(buf, sizeof(buf), ...)`。
- 排查内部 DRAM 退化时，可使用 `RAM_LOG("stage")`（`src/core/ram_profile.h`）。

### 4.3 LTO、ODR、SPI 总线与 RTOS 纪律
- **严格遵守 ODR（`-flto -Werror=odr`）**：严禁同一 `struct` / `class` 因头文件宏条件不一致在不同 `.cc` 编译单元中产生不同内存布局。
- **严禁修改 `struct keyStroke`**（位于 `include/globals.h`，明确标注 `// DO NOT CHANGE IT!!!!!`）。
- **SPI 总线隔离（`src/main.cc`）**：严格遵守 `sdcardSPI(VSPI)` 与 `AUX_SPI(HSPI)` 分离及 `USE_HSPI_PORT` 路由逻辑，切勿破坏共享显示/触屏/SD 的 SPI 主机配置。
- **输入并发安全**：读写全局导航标志（`NextPress`、`PrevPress`、`UpPress`、`DownPress`、`SelPress`、`EscPress`、`touchPoint`）时遵循 `inputLock()` / `inputUnlock()` 约定。
- **看门狗与退出响应**：任何 `while` 阻塞循环中都必须包含 `vTaskDelay(pdMS_TO_TICKS(...))` / `yield()`，并提供明确的用户退出路径（`EscPress` / 返回键）。

---

## 5. UI、哪吒魔童主题与双形态交互规范

- **优先使用主题配色辅助函数**：统一调用 `src/core/display.h` 提供的 `uiSurfaceColor()`、`uiSurfaceHiColor()`、`uiBorderDimColor()`、`uiTextBrightColor()`、`uiTextMutedColor()`、`uiTextOnAccentColor()`、`blendColors()`、`uiDrawText16()`、`uiMeasureText16()`、`uiTruncateText16()`、`drawNezhaCrest()`、`drawNezhaRibbonRule()`、`buildHeatPalette()`；避免在通用控件中硬编码杂乱的 `TFT_*` RGB 常量。
- **双分辨率与双输入适配**：跨板卡功能必须同时良好适配：
  - `320×240` 触屏 + 轨迹球 + 全键盘（`lilygo-t-deck`）
  - `240×135` 三按键紧凑屏（`m5stack-cplus1_1`）
- **动态布局计算**：基于 `tftWidth`、`tftHeight`、`LW`、`LH` 与 `STATUS_BAR_HEIGHT` 计算布局，禁止写死在大屏有效但会在 `240×135` 裁切的魔数坐标。
- **UTF-8 / 中文安全截断**：使用 `bruce_i18n::utf8PixelWidth()`、`utf8TruncateToWidth()`、`utf8WrapToWidth()`、`utf8SafeCutBytes()` 测量与截断文本，杜绝多字节 UTF-8 中文乱码或越界。

---

## 6. 中英双语国际化（i18n）与 PROGMEM 点阵字库流水线

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

## 7. 代码风格与格式规范

- **C++ / C（`.cc` / `.h`）**：严格遵守 [`.clang-format`](./.clang-format)：
  - 基础风格：`LLVM`
  - 缩进：**4 空格**（`UseTab: Never`，`IndentWidth: 4`）
  - 单行上限：**110 列**（`ColumnLimit: 110`）
  - 花括号：`BreakBeforeBraces: Attach`，`AccessModifierOffset: -4`，`IndentCaseLabels: true`，`BinPackArguments: false`
- **Python（`scripts/build/*.py`、`tools/*.py`）**：兼容 Python 3.11+，并通过 `python3 -m py_compile` 语法检查。
- **内存与边界安全**：字符串格式化统一使用 `snprintf(..., sizeof(buf), ...)`；对所有无线报文、SD 文件解析与动态内存分配（`malloc` / `ps_malloc`）做完备边界与空指针校验。

---

## 8. 编译、验证与 CI 检查命令

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

## 9. Git 工作流与原子提交规范

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

## 10. 安全合规与合法授权红线

- 本固件的所有无线、射频、BLE、红外、RFID 与网络审计功能**仅限用于合法授权的安全研究、教学实验、硬件调试与个人实验室测试**（详见 [SECURITY.md](./SECURITY.md)）。
- 严禁在仓库中硬编码真实凭据、API Key（`wigleBasicToken`、`wdgwarsApiKey`）或未授权目标信息。
- 严禁削弱 WebUI 会话认证、植入隐蔽遥测/数据回传或添加破坏性变砖逻辑。

---

## 11. 已适配的 AI 工具矩阵与同步原则

`AGENTS.md` 与 `AGENTS_ZH.md` 为核心权威规则；各工具配置文件作为轻量化入口保持同步：

| AI 工具 / 生态 | 规则文件路径 |
| :--- | :--- |
| **通用标准（Codex / OpenCode / Jules / Amp 等）** | `AGENTS.md`、`AGENTS_ZH.md` |
| **Claude Code** | `CLAUDE.md` |
| **Gemini CLI / Antigravity** | `GEMINI.md` |
| **Cursor** | `.cursor/rules/00-core-standards.mdc`、`10-esp32-firmware-memory.mdc`、`20-i18n-cjk-fonts.mdc` |
| **Trae IDE** | `.trae/rules/project_rules.md` |
| **GitHub Copilot** | `.github/copilot-instructions.md` |
| **Windsurf** | `.windsurf/rules/nezha-cyber.md` |
| **DeepSeek / Cline** | `.clinerules` |
| **DeepSeek / Roo Code** | `.roo/rules/nezha-cyber.md` |
| **DeepSeek / Continue** | `.continue/rules/nezha-cyber.md` |
| **DeepSeek / Aider** | `.aider.conf.yml` |

若核心工程规范发生变更，请同步更新 `AGENTS.md`、`AGENTS_ZH.md` 及对应工具适配器。
