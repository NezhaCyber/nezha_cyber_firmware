# Real-Time Logging (Logcat-style) — Nezha Cyber

The firmware now ships a lightweight, Android **logcat**-style logger designed for
crash diagnosis on real hardware. It is intentionally *not* a RAM ring buffer:
every line is appended to the SD card and flushed immediately, so the log
survives a panic, watchdog reset, brownout or battery pull.

## Log format

```text
MM-DD HH:MM:SS.mmm L/TAG (task): message
```

| Field | Meaning |
| :--- | :--- |
| `MM-DD HH:MM:SS.mmm` | Wall-clock timestamp once the RTC/NTP is set; otherwise `T+uptime.ms`. |
| `L` | `V` verbose, `D` debug, `I` info, `W` warn, `E` error, `F` fatal. |
| `TAG` | Short subsystem name (`BOOT`, `SD`, `WIFI`, `WEBUI`, `DEAUTH`, `IR`, `IRJAM`, `TVBGONE`, `MENU`, `UI`, `TASK`). |
| `task` | FreeRTOS task name (`loopTask`, `InputHandler`, `wifiConnectTask`, ...). |
| `message` | The event. Embedded newlines are folded so one event is one line. |

Example:

```text
11-27 10:45:12.123 I/MENU (loopTask): [WiFi] -> Enhanced Deauth
11-27 10:45:15.440 I/DEAUTH (loopTask): station deauth target=aa:bb:cc:dd:ee:ff ap=11:22:33:44:55:66 ch=6 band=0 mesh=2
```

## Files on the SD card

| Path | Purpose |
| :--- | :--- |
| `/NezhaLogs/nezha.log` | Live append-only log (flushed after every line). |
| `/NezhaLogs/nezha.log.1` | Previous file after automatic rotation (512 KiB). |

A boot banner is written at every startup, including the **ESP32 reset reason**.
If the previous run ended in a panic/watchdog/brownout, an `E/BOOT` line says so;
the last lines before the banner show exactly which action was running.

## Where to read it

1. **On the device** — `Config → System Log`:
   - *View live log*: streams the tail of the SD file (FOLLOW/PAUSED, 8 KiB
     window, level-coloured lines). The viewer keeps no history in RAM.
   - *Log Level*: changes the minimum level written to SD (default `DEBUG`).
   - *Clear log files*: removes the live and rotated file.
2. **Serial / USB** — every line is mirrored to `Serial` at `INFO` and above by
   default. Use the CLI:
   ```text
   log tail [lines]          # print the last N lines (default 30)
   log level [v|d|i|w|e|f]   # get/set SD level
   log serial [v|d|i|w|e|f]  # get/set serial mirror level
   log info                  # path, size, sink status
   log clear                 # delete live + rotated file
   ```
3. **WebUI (browser, real time)** — open:
   ```text
   http://<device-ip>/logs          # plain-text tail
   http://<device-ip>/logs?html=1   # auto-refreshing dark logcat viewer
   http://<device-ip>/logs?bytes=65536
   ```
   The endpoint uses the normal WebUI login/session.

## What gets logged

- Boot, reset reason, heap/PSRAM, SD mount/unmount, LittleFS.
- Every menu/app launch (`I/MENU [Menu] -> Item`) and every back action.
- Every on-screen error, warning, success and status line (`UI` tag).
- Wi-Fi connect/disconnect outcomes, WebUI start/stop.
- Full deauth lifecycle: target, AP, channel, mesh size, per-second FPS,
  raw-TX failures, stop summary and heap after the attack.
- IR send/replay/capture, TV-B-Gone, IR jammer start/stop.
- Any long-running tool that uses `progressHandler()` logs completion.

## Design constraints

- **No heap allocation per line** (fixed 320-byte stack buffer).
- **No RAM history**: the file is the only store.
- **Safe under recursion**: a log call made from inside the SD/FAT stack falls
  back to serial instead of deadlocking; concurrent tasks serialise on a
  recursive FreeRTOS mutex.
- **Bounded write cost**: log at state changes, not per injected frame.
