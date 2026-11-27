# Hardware Verification Checklist — Nezha Cyber

Run these on the real board after flashing. The SD log (`/NezhaLogs/nezha.log`)
shows each step; `log tail` over serial and `http://<ip>/logs?html=1` are
equivalent live views.

## 1. Logger smoke test

| Step | Expected screen | Expected log |
| :--- | :--- | :--- |
| Boot with SD inserted | normal splash/menu | `I/BOOT ... reset reason` + `I/SD mounted successfully` in `nezha.log` |
| `Config → System Log → View live log` | tail scrolls, FOLLOW green | each menu press appears as `I/MENU` |
| Open/close several apps | menu returns | `I/MENU [Main Menu] -> ...` and `D/MENU ... heap=...` lines |
| Pull SD while running | UI continues | `E/...` serial lines only; NO crash |
| Reboot after a forced panic | boots | `E/BOOT ABNORMAL PREVIOUS SHUTDOWN (PANIC)` |
| `log tail 50` in CLI | last 50 lines | file content matches |
| `http://<ip>/logs?html=1` | dark auto-refresh logcat page | updates every second |

## 2. WiFi deauthenticator

1. `WiFi → Wifi Atks → Enhanced Deauth → Deauth Target List`.
2. Scan, pick an AP, let the client scan finish.
3. Select a client → `Station Deauth`; press BACK to stop.

Expected log lines:
```text
I/DEAUTH mesh: target="<SSID>" aps=N
I/DEAUTH station deauth target=<STA> ap=<AP> ch=<N> band=0 mesh=N
I/DEAUTH attack radio ready: AP mode channel=<N>
D/DEAUTH station fps=... total=... storm=...
I/DEAUTH station deauth stopped: frames=... bursts=... tx_fail=...
I/DEAUTH mem @...
E/UI ...            (only on real failure)
```
The screen must show a non-zero increasing frame total. If `tx_fail` keeps
rising, check the AP/STA interface and channel; a warning is logged with the
ESP error name.

Also test `Enhanced Deauth → Deauth All Clients → Select Channel` and
`Select from Scan`: the status line shows the AP count; the client should drop.

## 3. IR feature

| Path | Expected |
| :--- | :--- |
| `Infrared → TV-B-Gone → Region NA/EU` | LED flashes; `I/TVBGONE starting`, `I/TVBGONE finished` |
| `Infrared → Custom IR → SD Card → file → Spam all` | codes replay in order; `I/IR replaying ...`; no skipped blocks |
| `Infrared → Custom IR → file → Choose cmd` | one command per press; `D/IR decoded send ...` |
| `Infrared → Universal IR` | brand bar + 6 buttons; each button transmits |
| `Infrared → IR Read → Custom Read` | receiver waits, shows protocol/bits/raw; save writes `.ir` |
| `Infrared → IR Jammer` | LED activity, counters rise; `I/IRJAM jammer start/stop` |

## 4. Memory / stability

- After 10 deauth start/stop cycles, the `mem @...` lines should not trend
  downward by more than a few KiB.
- Run the BLE file transfer receive/send once; after exit, enter it again;
  no double-free/reboot.
- Load a deliberately long/odd `.rfid` dump and a `.sub` without `Bit:` lines;
  the UI should reject or clamp them, not reboot.
- Enter `Responder` and connect a laptop to SMB; malformed SMB packets must
  not reboot the board.
- Use `log level d` before a long session, then `log level i` afterwards.

## 5. Build

```sh
pio run -e lilygo-t-deck
pio run -e m5stack-cplus1_1
```
The only environment-specific failure seen during development was PlatformIO
failing to copy a git pack inside a Termux/proot rootfs (`shutil.Error ...
Invalid argument`). That is a host/container limitation, not a firmware error;
run the build on a regular Linux/macOS/Windows laptop.
