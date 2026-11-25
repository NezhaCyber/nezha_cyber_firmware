# LILYGO T-Deck Standalone Wi-Fi Internet Radio

Complete standalone PlatformIO ESP32-S3 firmware for the **LILYGO T-Deck** (`ESP32-S3FN16R8`, 16 MB Flash, 8 MB OPI PSRAM) streaming live HTTP/HTTPS MP3 & AAC Icecast/Shoutcast internet radio over its built-in **MAX98357A I2S speaker** with a high-contrast **ST7789 320×240** dashboard.

## Hardware Pinout
| Subsystem | Signal | GPIO |
|---|---|---|
| Power Rail | `BOARD_POWERON` (HIGH at boot) | `10` |
| I2S Speaker (MAX98357A) | `I2S_BCLK` / `I2S_LRC` / `I2S_DOUT` | `7` / `5` / `6` |
| ST7789 LCD (320×240) | `CS` / `DC` / `SCLK` / `MOSI` / `BL` | `12` / `11` / `40` / `41` / `42` |
| MicroSD / Shared SPI | `SD_CS` / `MISO` / `LORA_CS` | `39` / `38` / `9` |
| Trackball Hall Sensors | `UP` / `DOWN` / `LEFT` / `RIGHT` / `CLICK` | `3` / `15` / `1` / `2` / `0` |
| ESP32-C3 Keyboard (I2C) | `SDA` / `SCL` / `ADDR` | `18` / `8` / `0x55` |

## Controls
- **Trackball Up / Down** (or keyboard `+` / `-`): Volume (`0–21`)
- **Trackball Left / Right** (or keyboard `p` / `n`): Previous / Next station
- **Trackball Click** (or keyboard `m` / Space): Mute / Unmute
- **Keyboard `1`–`9`**: Jump directly to preset channel
- **Keyboard `w`**: Interactive Wi-Fi setup (enter SSID & password on T-Deck keyboard or USB Serial)
- **Keyboard `r`**: Reconnect active stream

## MicroSD Configuration
Copy the templates from `data/` to the root of a FAT32 microSD card:
- `/stations.json` — custom station list (`name`, `url`, `genre`); falls back to built-in BBC World Service / BFM 89.9 / TraxxFM / NPR / SomaFM / Radio Paradise presets if omitted.
- `/wifi.json` — `{"ssid": "...", "password": "..."}`; falls back to NVS or interactive keyboard/serial prompt if omitted.

## Build & Flash via USB-C
```bash
cd projects/tdeck-internet-radio
pio run -t upload
pio device monitor
```
