# Wi-Fi CSI Through-Wall Motion / Presence Radar

> **Language / 语言:** **English** | [简体中文 (Simplified Chinese)](WIFI_CSI_RADAR_ZH.md)

The **Wi-Fi CSI Motion Radar** (`src/modules/wifi/csi_radar.{h,cc}` & `src/modules/wifi/csi_radar_dsp.h`) turns any Nezha Cyber ESP32 / ESP32-S3 device into a pure-software RF motion and presence detector using **Wi-Fi Channel State Information (CSI)** — with zero additional hardware required.

---

## How It Works

1. **Subcarrier Amplitude Extraction & AGC Normalization (`csi_radar_dsp.h`)**
   - When ESP-IDF CSI (`CONFIG_ESP_WIFI_CSI_ENABLED`) is active, `esp_wifi_set_csi_rx_cb()` delivers signed `(imag, real)` OFDM subcarrier I/Q samples (`L-LTF` / `HT-LTF`) for packets received from a locked stationary Access Point.
   - Each frame's amplitude profile `|H(k)| = sqrt(I² + Q²)` is normalized by its frame mean to **cancel Wi-Fi Automatic Gain Control (AGC) step artifacts**.
2. **Adaptive Multipath Baseline & Presence Scoring**
   - During the initial **Calibration (`CALIB`)** phase (24 frames), the tracker learns the static room multipath signature and ambient noise floor.
   - Human presence or movement across the Wi-Fi path perturbs individual OFDM subcarriers differently, raising the normalized delta above the ambient floor:
     - **`STILL`** (Green): Ambient floor
     - **`PRESENCE`** (Yellow): Subtle motion / presence (`>= 1.85×` ambient floor)
     - **`MOTION!`** (Red): Walking / through-wall motion (`>= 3.2×` ambient floor)
3. **Graceful RSSI-Variance Fallback**
   - On SoCs or builds without `CONFIG_ESP_WIFI_CSI_ENABLED`, the engine seamlessly falls back to locked-AP promiscuous **RSSI multipath variance** tracking so the radar remains usable on every board.

## Usage

- Open **WiFi → CSI Motion Radar**:
  - **Lock Target AP (Best)**: Scan nearby stationary routers and lock onto one AP's BSSID + channel for cleanest through-wall sensitivity.
  - **Channel Watch**: Monitor all frames on a chosen channel (`1–13`).
  - **Controls during live radar**:
    - `OK / Sel`: Re-calibrate room baseline
    - `Up / Down` or `Prev / Next`: Cycle Sensitivity (`LOW` / `MED` / `HIGH`)
    - `ESC`: Cleanly disable CSI & promiscuous callbacks and return
  - Optional CSV telemetry logging to `/NezhaLogs/csi_*.csv` (`ts_ms,score,state,rssi,pkt_rate`).

## Host-Native DSP Unit Test

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Isrc test/test_csi_dsp.cc -o /tmp/test_csi_dsp && /tmp/test_csi_dsp
```
