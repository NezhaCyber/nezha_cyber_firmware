# 硬件在环（HIL）冒烟测试与 WebSerial 自动发布指南

> **语言 / Language:** [English](HIL_TESTING.md) | **简体中文 (Simplified Chinese)**

本文档介绍 **哪吒网络安全（Nezha Cyber）** 固件的 USB-C 自动化硬件冒烟测试套件（`tools/hil_smoke_test.py`）以及 GitHub Pages WebSerial 网页烧录器的自动部署流程。

---

## 1. USB-C 自动化硬件冒烟测试

`tools/hil_smoke_test.py` 通过 USB-C 串口直连已烧录固件的 ESP32 / ESP32-S3 开发板，在发布版本前自动验证核心子系统与内存基线：

| 测试项 | 串口 CLI 命令 | 通过标准 |
| :--- | :--- | :--- |
| 设备信息 | `info` | 返回有效板型信息且无 `ERROR` |
| 运行时钟 | `uptime` | 返回有效单调运行时间 |
| 堆内存报告 | `free` | 返回有效数字堆余量 |
| 乾坤仪遥测与 DMA 下限 | `sysmon` | `heap_free >= min_heap_kb`、`dma_largest >= min_dma_kb`、`wifi_ok == true`、`cpu_mhz > 0` |
| 存储根目录遍历 | `storage list /` | LittleFS/SD 正常响应目录列表 |
| UART 串口状态 | `wire uart status` | 返回 `baud=` 与引脚状态 |
| I2C 总线仲裁 | `wire i2c scan` | 经由 `I2CLease` 正常完成扫描且无总线锁死 |

### 使用方式

```bash
# 在宿主机安装一次 pyserial
pip install pyserial

# 连接 LilyGO T-Deck / Cardputer / StickC Plus 执行真实硬件冒烟测试
python3 tools/hil_smoke_test.py --port /dev/ttyACM0 --baud 115200

# 针对 16 MB PSRAM 开发板设置更严格内存门槛并输出 JSON 报告
python3 tools/hil_smoke_test.py \
  --port /dev/ttyACM0 \
  --min-heap-kb 64 \
  --min-dma-kb 24 \
  --json-out hil_report.json

# 无硬件自检模式（GitHub Actions CI 也会自动执行）
python3 tools/hil_smoke_test.py --mock
```

---

## 2. WebSerial 烧录清单与 GitHub Pages 自动部署

- **清单生成脚本**：`tools/gen_web_manifests.py` 自动为全部 12 款官方发布开发板生成符合 `esp-web-tools` v10 规范的 `<env>.json` 与 `boards.json`。
- **自动发布流水线**：`.github/workflows/pages.yml` 在每次发布 GitHub Release（或手动触发 `workflow_dispatch`）时，自动打包 `tools/web_companion/`、生成对应版本号的烧录清单，并部署至 GitHub Pages。

### 本地生成与预览

```bash
python3 tools/gen_web_manifests.py --out-dir tools/web_companion/manifests --tag latest
cd tools/web_companion
python3 -m http.server 8080
```
使用 Chrome 或 Edge 打开 `http://localhost:8080`，即可体验一键 WebSerial 烧录、USB-C 实时命令行、远程导航遥控以及 `sysmon` 乾坤仪健康监控。
