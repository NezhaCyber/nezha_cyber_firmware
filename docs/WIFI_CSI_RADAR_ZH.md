# Wi-Fi CSI 穿墙移动与存在感知雷达

> **语言 / Language:** [English](WIFI_CSI_RADAR.md) | **简体中文 (Simplified Chinese)**

**Wi-Fi CSI 移动雷达**（`src/modules/wifi/csi_radar.{h,cc}` 与 `src/modules/wifi/csi_radar_dsp.h`）利用 ESP32 / ESP32-S3 原生 **Wi-Fi 信道状态信息（CSI）** 实现纯软件无线电移动与人体存在监测，无需任何额外外接传感器。

---

## 工作原理

1. **OFDM 子载波幅值提取与 AGC 归一化（`csi_radar_dsp.h`）**
   - 在启用 `CONFIG_ESP_WIFI_CSI_ENABLED` 的固件中，`esp_wifi_set_csi_rx_cb()` 会对锁定路由器发出的每一帧提供有符号 `(imag, real)` OFDM 子载波 I/Q 数据。
   - 算法对每帧子载波幅值 `|H(k)| = sqrt(I² + Q²)` 执行帧均值归一化，**消除 Wi-Fi 自动增益控制（AGC）跳变带来的误报**。
2. **自适应多径基线与三级状态判定**
   - 启动后前 24 帧进入 **校准阶段（`CALIB`）**，学习静止环境的多径基线与噪声底噪。
   - 当人体进入房间或穿墙经过信号路径时，各子载波产生差异化扰动，实时计算 `0–100` 移动评分：
     - **`STILL`（绿色）**：环境静止
     - **`PRESENCE`（黄色）**：微动 / 存在感知（超过底噪 `1.85×`）
     - **`MOTION!`（红色）**：显著移动 / 穿墙走动（超过底噪 `3.2×`）
3. **自动 RSSI 方差回退**
   - 若目标芯片或构建配置未启用 `CONFIG_ESP_WIFI_CSI_ENABLED`，引擎会自动回退至锁定 AP 的混杂模式 **RSSI 多径抖动监测**，确保全系列开发板稳定可用。

## 使用方法

- 进入 **WiFi → CSI Motion Radar**：
  - **Lock Target AP (Best)**：扫描并锁定附近一个固定路由器的 BSSID 与信道（穿墙效果最佳）。
  - **Channel Watch**：监听指定信道（`1–13`）全部帧。
  - **实时界面快捷键**：
    - `OK / Sel`：重新校准当前环境基线
    - `Up / Down` 或 `Prev / Next`：切换灵敏度（`LOW` / `MED` / `HIGH`）
    - `ESC`：安全关闭 CSI 与混杂回调并退出
  - 支持可选 SD 卡 CSV 遥测日志：`/NezhaLogs/csi_*.csv`（`ts_ms,score,state,rssi,pkt_rate`）。

## 宿主机 DSP 单元测试

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Isrc test/test_csi_dsp.cc -o /tmp/test_csi_dsp && /tmp/test_csi_dsp
```
