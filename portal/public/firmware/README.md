# Nezha Cyber Firmware Artifacts (`public/firmware/`)

Place compiled `.bin` images under `releases/<version>/` matching the paths declared in `manifest.json`:

```text
public/firmware/
├── manifest.json
└── releases/
    └── v2.1.0/
        ├── NezhaCyber-lilygo-t-deck.bin          # 0x0 (ESP32-S3 merged)
        ├── NezhaCyber-lilygo-t-deck-pro.bin      # 0x0 (ESP32-S3 merged)
        ├── NezhaCyber-lilygo-t-embed-cc1101.bin  # 0x0 (ESP32-S3 merged)
        ├── NezhaCyber-m5stack-cardputer.bin      # 0x0 (ESP32-S3 merged)
        ├── NezhaCyber-m5stack-cplus1_1.bin       # 0x0 (ESP32 merged)
        └── lilygo-t-deck/                        # Optional multi-partition layout
            ├── bootloader.bin                    # 0x0 (ESP32-S3) / 0x1000 (ESP32)
            ├── partitions.bin                    # 0x8000
            └── firmware.bin                      # 0x10000
```

## Offsets Verified Against `scripts/build/build.py` & `partitions/`

| MCU Family | Bootloader | Partition Table | App (`factory`) | Merged Factory Image |
| :--- | :---: | :---: | :---: | :---: |
| `ESP32-S3` (T-Deck / T-Deck Plus / Cardputer / T-Embed) | `0x0000` | `0x8000` | `0x10000` | `0x0` |
| `ESP32` (M5StickC Plus 1.1 / CYD) | `0x1000` | `0x8000` | `0x10000` | `0x0` |
| `ESP32-C5` | `0x2000` | `0x8000` | `0x10000` | `0x0` |

> You can also flash any locally compiled `.bin` file directly in the portal using the **Local Binary** tab without copying it here.
