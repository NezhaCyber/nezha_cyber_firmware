<p align="center">
  <img src="./media/pictures/nezha_cyber_banner.jpg" alt="哪吒网络安全 (Nezha Cyber)" width="200" />
</p>

<h1 align="center">哪吒网络安全 (Nezha Cyber)</h1>

<p align="center">
  基于 ESP32 / ESP32-S3 的便携式无线安全研究、射频与硬件工具固件。<br />
  <a href="./README.md">English</a> | <strong>简体中文</strong>
</p>

<p align="center">
  作者与维护者：<strong>钟智强 (<a href="https://github.com/ctkqiang">ctkqiang</a>)</strong> · 开源协议：<strong>GNU AGPL-3.0</strong>
</p>

---

## 项目简介

**哪吒网络安全 (Nezha Cyber)** 是基于 [Bruce Firmware](https://github.com/pr3y/Bruce) 独立演进的开源固件分支。项目完整保留了 Bruce Firmware 的全部上游 Git 提交历史与开源致谢，但在产品方向与界面设计上独立发展：

- **默认简体中文 + 英文双语**：固件默认语言为简体中文（`zh-CN`），可在设置中随时切换英文（`en`），采用紧凑型 12px/16px PROGMEM 点阵中文字库，兼容 4 MB Flash 小容量板卡。
- **统一工业硬件 UI 与哪吒魔童主题**：重构启动器、菜单、弹窗、软键盘、BLE 工具与设置界面；内置 20 套哪吒魔童主题配色（共 71 套配色）、实时预览主题工坊、3 种主题纹饰、7 种主菜单布局与 5 秒动态开机画面（任意键/触屏跳过）。
- **独立哪吒宠物与日常硬件工具**：移除旧版 `Brucegotchi` / `PwnGrid` / `Megalodon`，内置原创持久化**哪吒宠物**；新增 Sub-GHz 频率分析仪、万能红外遥控、GPIO 示波器与引脚监测、2.4 GHz Wi-Fi 热力瀑布图、TOTP 二步验证、PWM 信号发生器、摩斯电码发射、离线哈希实验室与五声音阶合成器。

---

## 功能概览

| 分类 | 模块 |
| :--- | :--- |
| **界面与主题** | 7 种主菜单布局（网格/轮播/列表/卡片/停靠栏/分栏/紧凑网格）、哪吒魔童主题工坊（20 套主题 / 71 套配色）、3 种主题纹饰、默认简体中文 / 英文 |
| **诊断与日志** | Logcat 风格实时系统日志（写入 SD 卡 `/NezhaLogs/nezha.log`）、设备端实时查看器、串口 CLI（`log tail/level/info/clear`）、WebUI `/logs` 实时页面、复位原因与崩溃检测 |
| **Wi-Fi** | STA/AP、抓包嗅探、信道分析、2.4 GHz 热力瀑布图、干扰检测、Probe 监听、局域网端口与 Banner 审计、IDS 哨兵、Evil Portal、Responder、ARP 扫描、WireGuard、SSH/Telnet、SOCKS4 |
| **BLE 蓝牙** | 可滚动 BLE 广播测试界面、BLE 扫描、GATT 探测与通知监听、BLE 文件传输、BadBLE 与蓝牙键盘 |
| **战术雷达** | 实时 Wi-Fi / BLE 空间雷达、告警标记、全局 GPS/RF 定位状态、可开关雷达提示音 |
| **Sub-GHz 与红外** | CC1101 / RF433 频率分析仪、频谱、瀑布图、录制重放、ProtoPirate、万能红外遥控器、TV-B-Gone、自定义红外 |
| **RFID 与无线电** | PN532、PN532Killer、Chameleon、Amiibolink、LoRa、FM 广播、NRF24 2.4G 频谱 |
| **硬件与日常工具** | GPIO 引脚控制与电压监测、便携 GPIO 示波器、UART / I2C / SPI / JTAG / 通断检测、TOTP 二步验证、PWM 信号发生器、摩斯电码发射、离线密码哈希实验室（`SHA-256`/`SHA-1`/`MD5`/`Base64`）、硬件随机密码、哪吒合成器、二维码、BadUSB/HID、JS 解释器、哪吒宠物 |

---

## 主要验证板卡

| 开发板 | 芯片 | Flash / PSRAM | PlatformIO 环境 |
| :--- | :--- | :--- | :--- |
| **LilyGO T-Deck / T-Deck Plus** | ESP32-S3 | 16 MB / 8 MB OPI PSRAM | `lilygo-t-deck`、`lilygo-t-deck-pro` |
| **M5StickC Plus 1.1** | ESP32 | 4 MB | `m5stack-cplus1_1` |

其余社区开发板定义（Cardputer、Core2、CoreS3、M5StickC Plus2、T-Embed CC1101、CYD-2432S028、ESP32-C5 等）见 [`boards/`](./boards/)。

---

## 安装与编译

### 1. 直接烧录 GitHub Releases 预编译固件
从 **GitHub Releases** 下载对应板卡的合并固件（`NezhaCyber-<板卡>.bin`）或 OTA 固件（`NezhaCyber-<板卡>-ota.bin`），从偏移 `0x00000` 烧录：

```sh
# LilyGO T-Deck (ESP32-S3)
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x00000 NezhaCyber-lilygo-t-deck.bin

# M5StickC Plus 1.1 (ESP32)
esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash 0x00000 NezhaCyber-m5stack-cplus1_1.bin
```

### 2. 本地源码编译与烧录
```sh
# 自动识别已连接的 T-Deck 或 M5StickC Plus 1.1 并编译烧录
make install

# 指定板卡快捷烧录
make tdeck
make stickc-plus

# 仅编译合并固件 -> NezhaCyber-<env>.bin
make build ENV=lilygo-t-deck
make build ENV=m5stack-cplus1_1

# 或直接使用 PlatformIO
pio run -e lilygo-t-deck -e m5stack-cplus1_1
```

**LilyGO T-Deck 进入下载模式**：电源拨至 OFF → 按住轨迹球（`GPIO0`）→ 电源拨至 ON → 松开轨迹球 → 执行 `make tdeck`。

### 3. 更新中文翻译与字库
修改 [`src/core/lang_zh_cn.h`](./src/core/lang_zh_cn.h) 后执行：
```sh
python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check
```

---

## 目录结构

完整说明见 [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md)。

- `src/core/` — 核心系统、显示驱动、主题、中英文 i18n、配置、启动器、WebUI、串口命令
- `src/modules/` — Wi-Fi、BLE、RF、IR、RFID、Wire Tools、雷达、Flipper 工具、哪吒宠物
- `boards/` — 各开发板引脚与 PlatformIO 配置
- `partitions/` — 4 MB / 8 MB / 16 MB 分区表
- `scripts/build/` — PlatformIO 构建钩子与发布打包脚本
- `tools/` — 主机端工具（`gen_cjk_font.py`）
- `.github/` — CI 编译检查、自动 GitHub Release 发布、Issue 与 PR 模板

相关文档：[AGENTS_ZH.md](./AGENTS_ZH.md) · [CONTRIBUTING.md](./CONTRIBUTING.md) · [SECURITY.md](./SECURITY.md) · [CHANGELOG.md](./CHANGELOG.md) · [docs/ROADMAP.md](./docs/ROADMAP.md)

---

## 致谢与协议

由 **钟智强 ([ctkqiang](https://github.com/ctkqiang))** 维护。  
项目分支自 **[pr3y/Bruce](https://github.com/pr3y/Bruce)**，感谢 Bruce 社区及上游开源项目（`ESP32Marauder`、`Launcher`、`Evil-M5Project`、`M5Stick-Nemo`）。第三方组件许可见 [THIRD_PARTY.md](./THIRD_PARTY.md)。

遵循 **GNU AGPL-3.0** 协议开源（[LICENSE](./LICENSE)）。本固件仅限用于合法授权的安全研究、教学、硬件调试与个人实验室测试。
