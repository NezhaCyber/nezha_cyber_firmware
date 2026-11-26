<p align="center">
  <img src="./media/pictures/nezha_cyber_banner.jpg" alt="哪吒网络安全 (Nezha Cyber) — 哪吒魔童" width="260" />
</p>

<h1 align="center">🔥 哪吒网络安全 (Nezha Cyber)</h1>

<p align="center">
  <strong>哪吒魔童主题 · 便携式 ESP32 无线安全研究与硬件工具箱固件</strong><br />
  <em>我命由我不由天 · My fate is mine, not heaven's!</em>
</p>

<p align="center">
  <a href="./README.md">English</a> | <strong>简体中文</strong>
</p>

<p align="center">
  <strong>作者与维护者：</strong>钟智强 (<a href="https://github.com/ctkqiang">ctkqiang</a>) ·
  <strong>开源协议：</strong>AGPL-3.0
</p>

---

## ⚡ 项目定位与独立分支声明

**哪吒网络安全 (Nezha Cyber)** 是由 **钟智强 (ctkqiang)** 主导开发与维护的独立硬件安全固件项目。

本项目**基于 [Bruce Firmware](https://github.com/pr3y/Bruce) 独立分支（Standalone Fork）演进而来**，并完整保留了 Bruce Firmware 的全部历史 Git 提交记录与上游开源致谢。由于我们的产品理念与设计哲学截然不同，**哪吒网络安全**走上了独立演进路线：

- **工业级硬件 OS 视觉体系**：全面重构启动器、菜单、软键盘、雷达、BLE 工具、主题工坊与全局设置界面，打造克制、高对比度的暗曜石工业硬件交互体验，摒弃杂乱老旧界面。
- **原生「哪吒魔童」国风赛博美学**：内置高精 **哪吒魔童** 开机徽章、**20 套哪吒魔童主题套装**（共 71 套配色方案）、**3 种哪吒魔童主题纹饰**（魔童烈焰 / 莲花玄印 / 冰火双生）以及实时可视化主题工坊。
- **100% 原创「哪吒宠物」交互伴侣**：彻底移除旧版 Bruce 的 `Brucegotchi` / `PwnGrid` / `Megalodon` 模块，打造全新持久化 **哪吒宠物**（4 大修炼境界 + 喂食/修炼/互动/被动灵力巡查/打坐），同时原生支持 T-Deck 与 M5StickC Plus 1.1。
- **8 国语言原生国际化体验**：自研紧凑型 PROGMEM Unicode/CJK 位图字库管线，开箱支持 **简体中文、English、한국어（韩语）、日本語（日语）、Deutsch（德语）、Русский（俄语）、Español（西班牙语）、Français（法语）** 一键切换，并在 **4 MB Flash** 小容量设备（M5StickC Plus 1.1）与 **16 MB + 8 MB PSRAM** 旗舰设备（LilyGO T-Deck）上分级自适应运行。
- **Flipper 风格日常硬件工具箱**：内置 Sub-GHz 频率分析仪、万能红外遥控器、GPIO 引脚控制与电压监测、RFC 6238 TOTP 二步验证器、PWM 信号发生器、摩斯电码发射器、硬件真随机密码生成器与多面骰子。

---

## ✨ 核心亮点功能

### 🎨 哪吒魔童主题工坊 & 7 种主菜单布局
- **哪吒魔童主题工坊**（`设置 → 显示与界面 → 哪吒魔童主题`）：全屏实时预览哪吒魔童徽章、主色/辅色/背景色块与按钮效果，一键应用 **20 套哪吒魔童主题**：
  - 哪吒魔童·魔丸赤焰 · 混天绫·绯红流绸 · 风火轮·烈焰流金 · 乾坤圈·鎏金玄铁 · 火尖枪·炽红锋芒 · 莲花化身·绯粉圣莲 · 魔丸封印·暗紫冥火 · 灵珠敖丙·冰龙苍蓝 · 冰火双生·哪吒敖丙 · 东海龙宫·深海玄青 · 陈塘关·朱墙黛瓦 · 天劫神雷·紫电破天 · 山河社稷·青绿水墨 · 太乙仙风·玉白金霞 · 申公豹·幽冥玄墨 · 三头六臂·焚天赤金 · 玉虚宫·玄穹天光 · 魔童闹海·炽浪沧渊 · 哪吒绯红 · 哪吒莲火。
- **主题纹饰**（`设置 → 显示与界面 → 主题纹饰`）：自由切换 `魔童烈焰`、`莲花玄印`、`冰火双生` 或 `极简硬件`。
- **7 种主菜单布局**（`设置 → 显示与界面 → 菜单布局`）：`网格`、`轮播`、`列表`、`卡片`、`停靠栏`、`分栏`、`紧凑网格`，全面兼容触控、轨迹球、全键盘与三键操作。

### 🐾 原创哪吒宠物（哪吒魔童电子伴侣）
- 位于 `其他工具 → 哪吒宠物`（同时可设为开机自启应用），状态自动持久化保存于 NVS。
- **4 大视觉进化境界**：`魔丸灵童 (Lv.1-3)` → `莲花化身 (Lv.4-6)` → `风火少年 (Lv.7-9)` → `哪吒三太子 (Lv.10+)`，随等级解锁莲花台、旋转风火轮与乾坤金环光轮。
- **5 种交互玩法**：**喂食** · **修炼** · **互动**（T-Deck 可直接点按哪吒互动）· **巡查**（纯被动 Wi-Fi 环境信号感知，无去认证干扰）· **打坐**。

### 📡 战术雷达 & 无线安全研究套件
- **战术雷达**：实时 Wi-Fi / BLE 空间扫描，支持追踪器/钓鱼热点/安全设备告警标记、全局 GPS/RF 定位状态，以及可开关的 **雷达提示音**（`设置 → 音频设置 → 雷达提示音`，默认开启）。
- **Wi-Fi 安全工具**：抓包嗅探、信道频谱分析、干扰检测、Probe 监听、局域网端口与 Banner 审计、IDS 哨兵、安全审计、Evil Portal、Responder、WireGuard、SSH/Telnet 等。
- **BLE 蓝牙工具**：全新重构的可滚动触控 BLE 广播测试界面、BLE 扫描、GATT 探测与通知监听、BLE 文件传输、BadBLE / 蓝牙键盘。
- **Sub-GHz / 红外 / NFC / LoRa / FM**：**Sub-GHz 频率分析仪**、**万能红外遥控器**、CC1101 频谱与瀑布图、PN532 / PN532Killer / Chameleon、LoRa 与 FM 广播。

### 🛠️ Flipper 风格日常工具 & 硬件总线工具
- **Flipper 工具箱**（`其他工具 → Flipper 工具`）：**2.4 GHz WiFi 热力瀑布图**、**便携 GPIO 示波器与逻辑绘图仪**、**哪吒古风五声音阶合成器与音效板**、**离线硬件密码实验室**（SHA-256 / SHA-1 / MD5 / Base64 / TRNG）、**TOTP 二步验证器**、**PWM 信号发生器**、**摩斯电码发射器**、**硬件随机密码生成器**、**多面骰子**。
- **硬件总线工具**：**GPIO 引脚控制与 ADC 实时电压监测**、UART 终端、I2C 扫描与寄存器读写、SPI Flash 控制台、JTAG 扫描、通断蜂鸣检测。

---

## 🚀 快速编译与烧录

仓库内置自动识别串口与板卡的 [Makefile](./Makefile)，开箱即支持 **LilyGO T-Deck**（ESP32-S3）与 **M5StickC Plus 1.1**（ESP32）。

### 1. 使用 `make` 一键编译与烧录（推荐）
```sh
# 自动检测已连接的 T-Deck 或 M5StickC Plus 1.1 并编译烧录
make install

# 指定板卡快捷烧录
make tdeck            # LilyGO T-Deck (lilygo-t-deck)
make tdeck-plus       # LilyGO T-Deck Plus (lilygo-t-deck-pro)
make stickc-plus      # M5StickC Plus 1.1 (m5stack-cplus1_1)
make cardputer        # M5Stack Cardputer (m5stack-cardputer)

# 仅编译合并固件 -> NezhaCyber-<ENV>.bin
make build ENV=lilygo-t-deck
make build ENV=m5stack-cplus1_1

# 直接使用 esptool 烧录合并固件（偏移 0x00000）
make flash-bin ENV=lilygo-t-deck
```

### 2. LilyGO T-Deck 进入下载模式
1. 使用 USB-C 数据线连接 T-Deck，并将电源开关拨至 **OFF**。
2. **按住轨迹球按键不放**（`GPIO0` / `BOOT`）。
3. 保持按住轨迹球的同时将电源拨至 **ON**，随后松开轨迹球。
4. 运行 `make tdeck` 即可完成烧录。

### 3. 更新中文字库（开发者）
修改 [`src/core/lang_zh_cn.h`](./src/core/lang_zh_cn.h) 后执行：
```sh
python3 tools/gen_cjk_font.py && python3 tools/gen_cjk_font.py --check
```

---

## 🙏 传承致谢与开源协议

**哪吒网络安全 (Nezha Cyber)** 由 **钟智强 ([ctkqiang](https://github.com/ctkqiang))** 创作与维护。

本项目作为独立演进分支起源于 **[Bruce Firmware](https://github.com/pr3y/Bruce)**，完整保留了原始提交历史，并严格遵循 **GNU AGPL-3.0** 开源协议。谨向 Bruce 团队及所有上游开源项目（`pr3y/Bruce`、`ESP32Marauder`、`Launcher`、`Evil-M5Project`、`M5Stick-Nemo` 及 [THIRD_PARTY.md](./THIRD_PARTY.md) 中列出的全部贡献者）致以诚挚谢意。

---

## ⚠️ 法律与合规声明

**哪吒网络安全 (Nezha Cyber)** 仅限用于**合法授权的网络安全研究、教学实验、硬件调试与个人实验室测试**。严禁用于任何未经授权的网络渗透、干扰或恶意用途。使用本固件即代表您同意遵守所在地法律法规，作者 **钟智强 (ctkqiang)** 及上游贡献者不对任何滥用行为承担责任。
