# Nezha Cyber Unit Test Suite (`test/`)

> **English & 简体中文** host-native unit test suite for **Nezha Cyber (哪吒网络安全)**.

Runs fast (`< 2s`), zero-hardware C++17 and Python unit tests directly on Linux/macOS hosts and GitHub Actions CI without needing an ESP32 toolchain or physical board.

## Quick Run

```bash
chmod +x test/run_tests.sh
./test/run_tests.sh
# or via Makefile:
make test
```

## Test Coverage

| Suite | File | What It Tests |
| :--- | :--- | :--- |
| **i18n & CJK Tables (16 MB + 4 MB Compact)** | [test_i18n_and_cjk.cc](cpp/test_i18n_and_cjk.cc) | Compiles real `src/core/lang_zh_cn.h` & `cjk_font_data.h` under both standard and `-DBRUCE_COMPACT_CJK_FONT=1`; verifies strict `strcmp` sorting, `O(log N)` binary search hits every entry, and 100% CJK glyph coverage. |
| **ScrollableTextArea Bounds** | [test_scrollable_bounds.cc](cpp/test_scrollable_bounds.cc) | OOB `getLine()`, unsigned underflow in `scrollToLine()`, width=1 indent wrap non-hang, `clear()` + `shrink_to_fit()`. |
| **Network, OUI & Paths** | [test_net_and_oui.cc](cpp/test_net_and_oui.cc) | MAC parser (`:` & `-` delimiters, malformed/overflow rejection), IPv4 formatting, `O(log N)` Samsung OUI binary search, extensionless `createNewFile` splitting. |
| **Zero-Alloc IR/RF & Wordlist Parsers** | [test_raw_parsers.cc](cpp/test_raw_parsers.cc) | `strtoul` IR RAW parser, signed `strtol` Sub-GHz RAW parser + sentinel, `WordlistReader` CRLF/EOF/overlong-line (`> cap`) non-hang. |
| **UART ANSI, CRC32, OBD-II & Logic Decoders** | [test_uart_and_wire.cc](cpp/test_uart_and_wire.cc) | VT100 CSI/OSC ANSI stripper, IEEE CRC32 (`0xCBF43926`) + incremental streaming, SAE J2012 DTC decoding (`P/C/B/U`), UART 8N1 & 1-Wire edge decoders. |
| **Radio Bounded Containers & Sentinel** | [test_radio_containers.cc](cpp/test_radio_containers.cc) | 64-entry BLE priority-preserving eviction, 64-entry Wi-Fi promiscuous POD pool deduplication, Evil-Twin mismatch vs mesh, BLE Stalker persistence (`>=3` epochs / `>=90s`), CC1101 3-streak jamming detector. |
| **Repo & Firmware Invariants (Python)** | [test_repo_invariants.py](python/test_repo_invariants.py) | Non-overlapping `partitions/*.csv` validation, `lang_zh_cn.h` sort & glyph coverage, `struct keyStroke` ABI guard, Python script compilation, and board `.ini` checks. |
