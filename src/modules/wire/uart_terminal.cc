#if !defined(LITE_VERSION)
#include "uart_terminal.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/utils.h"
#include "wire_tools.h"
#include <HardwareSerial.h>
#include <esp_timer.h>
#include <globals.h>

// Quick presets offered in the baudrate menu; anything else goes through "Custom"
static const uint32_t baudPresets[] = {
    1200,  2400,  4800,  9600,   14400,  19200,  28800,
    38400, 57600, 76800, 115200, 230400, 460800, 921600,
};
constexpr size_t BAUD_PRESET_COUNT = sizeof(baudPresets) / sizeof(baudPresets[0]);

// Range accepted by the ESP32 UART peripherals, same limits as the JS uart API
#define UART_BAUD_MIN 300U
#define UART_BAUD_MAX 5000000U

// Keep the terminal history bounded so internal DRAM stays healthy across all boards
constexpr size_t UART_TERM_MAX_LINES = 300;
constexpr size_t UART_TERM_FLUSH_LEN = 128;
constexpr size_t UART_TERM_INPUT_LEN = 128;
constexpr uint32_t UART_PROMPT_FLUSH_MS = 40;

enum class UartLineEnding : uint8_t { CRLF = 0, LF = 1, CR = 2, NONE = 3 };

struct UartFramePreset {
    const char *label;
    uint32_t config;
};

static const UartFramePreset framePresets[] = {
    {"8N1", SERIAL_8N1},
    {"8E1", SERIAL_8E1},
    {"8O1", SERIAL_8O1},
    {"7E1", SERIAL_7E1},
    {"8N2", SERIAL_8N2},
};
constexpr size_t FRAME_PRESET_COUNT = sizeof(framePresets) / sizeof(framePresets[0]);

// Session settings persisted in RAM across re-entries
static uint32_t uartBaud = 115200;
static uint8_t uartFrameIdx = 0; // 8N1
static UartLineEnding uartLineEnding = UartLineEnding::CRLF;
static bool uartLocalEcho = true;
static bool uartHexView = false;
static bool uartInvert = false;
static bool uartLogToSd = false;
// Set while a serial-command UART session is open, so the UI terminal refuses to steal Serial1
static bool wireUartActive = false;

static const char *lineEndingLabel(UartLineEnding le) {
    switch (le) {
        case UartLineEnding::CRLF: return "CRLF (\\r\\n)";
        case UartLineEnding::LF: return "LF (\\n)";
        case UartLineEnding::CR: return "CR (\\r)";
        case UartLineEnding::NONE: return "None";
    }
    return "CRLF";
}

static void sendLineEnding(HardwareSerial &port, UartLineEnding le) {
    switch (le) {
        case UartLineEnding::CRLF: port.print("\r\n"); break;
        case UartLineEnding::LF: port.print('\n'); break;
        case UartLineEnding::CR: port.print('\r'); break;
        case UartLineEnding::NONE: break;
    }
}

// Lightweight VT100/ANSI escape stripper so OpenWrt/Linux/U-Boot consoles stay readable.
struct AnsiStripper {
    enum State : uint8_t { NORMAL = 0, ESC = 1, CSI = 2, OSC = 3 };
    State state = NORMAL;

    // Returns true if byte should be emitted to display buffer.
    bool feed(uint8_t b) {
        switch (state) {
            case NORMAL:
                if (b == 0x1B) {
                    state = ESC;
                    return false;
                }
                return true;
            case ESC:
                if (b == '[') state = CSI;
                else if (b == ']') state = OSC;
                else state = NORMAL;
                return false;
            case CSI:
                if (b >= 0x40 && b <= 0x7E) state = NORMAL;
                return false;
            case OSC:
                if (b == 0x07 || b == 0x1B) state = NORMAL;
                return false;
        }
        state = NORMAL;
        return true;
    }
};

static int parseHexBytes(const String &hexIn, uint8_t *out, size_t maxLen) {
    String h = hexIn;
    h.trim();
    h.replace("0x", "");
    h.replace("0X", "");
    h.replace(" ", "");
    if (h.isEmpty() || (h.length() % 2) != 0) return -1;
    size_t n = h.length() / 2;
    if (n > maxLen) n = maxLen;
    for (size_t i = 0; i < n; ++i) {
        char pair[3] = {h[i * 2], h[i * 2 + 1], '\0'};
        char *end = nullptr;
        unsigned long v = strtoul(pair, &end, 16);
        if (end != pair + 2) return -1;
        out[i] = static_cast<uint8_t>(v);
    }
    return static_cast<int>(n);
}

static String formatHexRow(uint32_t offset, const uint8_t *buf, size_t len) {
    char line[64];
    int pos = snprintf(line, sizeof(line), "%04X:", (unsigned)(offset & 0xFFFF));
    for (size_t i = 0; i < 8; ++i) {
        if (i < len) pos += snprintf(line + pos, sizeof(line) - pos, " %02X", buf[i]);
        else pos += snprintf(line + pos, sizeof(line) - pos, "   ");
    }
    pos += snprintf(line + pos, sizeof(line) - pos, " |");
    for (size_t i = 0; i < len && pos + 2 < (int)sizeof(line); ++i) {
        line[pos++] = (buf[i] >= 32 && buf[i] <= 126) ? (char)buf[i] : '.';
    }
    if (pos + 2 < (int)sizeof(line)) {
        line[pos++] = '|';
        line[pos] = '\0';
    }
    return String(line);
}

static File openUartLogFile(String &outPath) {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) return File();

    if (!fs->exists("/NezhaLogs")) fs->mkdir("/NezhaLogs");
    for (uint16_t idx = 0; idx < 1000; ++idx) {
        char path[40];
        snprintf(path, sizeof(path), "/NezhaLogs/uart_%03u.log", (unsigned)idx);
        if (!fs->exists(path)) {
            outPath = path;
            return fs->open(path, FILE_WRITE);
        }
    }
    return File();
}

/*
 * Auto-baud detection: measures the shortest pulse width seen on the RX pin and
 * snaps it to the closest standard preset. Returns the detected baudrate, or 0
 * when too few transitions arrived before the timeout.
 */
uint32_t wire_uart_autobaud(int8_t rx, uint32_t timeoutMs) {
    if (rx < 0) rx = (int8_t)bruceConfigPins.uart_bus.rx;
    if (rx < 0) return 0;

    Serial1.end();
    pinMode(rx, INPUT_PULLUP);

    int prev = digitalRead(rx);
    int64_t lastEdgeUs = esp_timer_get_time();
    int64_t endUs = lastEdgeUs + (int64_t)timeoutMs * 1000LL;
    uint32_t minPulseUs = UINT32_MAX;
    uint32_t edges = 0;

    while (esp_timer_get_time() < endUs && edges < 160) {
        int cur = digitalRead(rx);
        if (cur != prev) {
            int64_t nowUs = esp_timer_get_time();
            uint32_t dt = (uint32_t)(nowUs - lastEdgeUs);
            // Ignore sub-1us glitches and overly long idle gaps
            if (dt >= 1 && dt < 20000 && edges > 0) {
                if (dt < minPulseUs) minPulseUs = dt;
            }
            lastEdgeUs = nowUs;
            prev = cur;
            edges++;
        }
    }

    if (edges < 4 || minPulseUs == UINT32_MAX) return 0;

    uint32_t estBaud = 1000000UL / minPulseUs;
    uint32_t best = baudPresets[0];
    uint32_t bestErr = UINT32_MAX;
    for (size_t i = 0; i < BAUD_PRESET_COUNT; ++i) {
        uint32_t b = baudPresets[i];
        uint32_t err = (estBaud > b) ? (estBaud - b) : (b - estBaud);
        if (err < bestErr) {
            bestErr = err;
            best = b;
        }
    }
    return best;
}

static void uart_autobaud_ui() {
    BruceConfigPins::UARTPins pins = bruceConfigPins.uart_bus;
    if (pins.rx < 0) {
        displayError("Set RX/TX pins first", true);
        return;
    }

    drawMainBorderWithTitle("Auto-Baud Detect");
    padprintln("");
    padprintln("Listening on RX GPIO " + String((int)pins.rx) + "...");
    padprintln("Trigger target UART traffic.");
    padprintln("");
    padprintln("Press ESC to cancel.");

    uint32_t detected = 0;
    for (int attempt = 0; attempt < 15 && !check(EscPress); ++attempt) {
        detected = wire_uart_autobaud((int8_t)pins.rx, 350);
        if (detected > 0) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (detected == 0) {
        displayWarning("No UART traffic detected", true);
        return;
    }

    uartBaud = detected;
    displaySuccess("Detected: " + String(detected) + " bps", true);
}

/*
 * Baudrate selection: the preset list plus a custom numeric entry clamped to the
 * range the ESP32 UART peripherals accept (300..5000000 bps).
 */
static void uart_select_baudrate() {
    bool isCustom = true;
    int selected = 0;

    std::vector<Option> localOpts;
    localOpts.reserve(BAUD_PRESET_COUNT + 1);
    for (size_t i = 0; i < BAUD_PRESET_COUNT; i++) {
        if (baudPresets[i] == uartBaud) {
            selected = (int)i;
            isCustom = false;
        }
        localOpts.push_back(
            {String(baudPresets[i]) + " bps", [i]() { uartBaud = baudPresets[i]; }, baudPresets[i] == uartBaud}
        );
    }
    localOpts.push_back(
        {"Custom",
         []() {
             String value = num_keyboard(String(uartBaud), 8, "Custom baudrate:");
             if (value == "\x1B") return;
             uint32_t baud = (uint32_t)strtoul(value.c_str(), nullptr, 10);
             if (baud < UART_BAUD_MIN || baud > UART_BAUD_MAX) {
                 displayError("Baud 300 - 5000000", true);
                 return;
             }
             uartBaud = baud;
         },
         isCustom}
    );

    loopOptions(localOpts, MENU_TYPE_SUBMENU, "Baudrate", selected);
}

static void uart_select_framing() {
    std::vector<Option> localOpts;
    localOpts.reserve(FRAME_PRESET_COUNT);
    for (size_t i = 0; i < FRAME_PRESET_COUNT; ++i) {
        localOpts.push_back(
            {String(framePresets[i].label), [i]() { uartFrameIdx = (uint8_t)i; }, i == uartFrameIdx}
        );
    }
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "Framing", uartFrameIdx);
}

static void uart_select_line_ending() {
    std::vector<Option> localOpts = {
        {"CRLF (\\r\\n)", []() { uartLineEnding = UartLineEnding::CRLF; }, uartLineEnding == UartLineEnding::CRLF},
        {"LF (\\n)",       []() { uartLineEnding = UartLineEnding::LF; },   uartLineEnding == UartLineEnding::LF  },
        {"CR (\\r)",       []() { uartLineEnding = UartLineEnding::CR; },   uartLineEnding == UartLineEnding::CR  },
        {"None",           []() { uartLineEnding = UartLineEnding::NONE; }, uartLineEnding == UartLineEnding::NONE},
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "Line Ending", (int)uartLineEnding);
}

/*
 * USB-C <-> Serial1 passthrough bridge, so the device acts as a field USB-UART
 * adapter. The serial CLI task is suspended while the bridge owns Serial1 and
 * resumed on exit.
 */
void uart_usb_bridge() {
    if (wireUartActive) {
        displayError("UART busy (serial cmd)", true);
        return;
    }
    if (USBserial.getSerialOutput() == &Serial1) {
        displayError("(E) UART already in use", true);
        return;
    }

    BruceConfigPins::UARTPins pins = bruceConfigPins.uart_bus;
    if (pins.rx < 0 || pins.tx < 0) {
        displayError("Set RX/TX pins first", true);
        return;
    }

    if (serialcmdsTaskHandle != nullptr) vTaskSuspend(serialcmdsTaskHandle);

    Serial1.end();
    Serial1.setRxBufferSize(4096);
    Serial1.begin(uartBaud, framePresets[uartFrameIdx].config, pins.rx, pins.tx, uartInvert);

    File logFile;
    String logPath;
    if (uartLogToSd) logFile = openUartLogFile(logPath);

    drawMainBorderWithTitle("USB-UART Bridge");
    tft.setTextSize(FP);
    tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
    padprintln("");
    padprintln("USB-C <-> UART1 (" + String(uartBaud) + " " + String(framePresets[uartFrameIdx].label) + ")");
    padprintln("RX: GPIO " + String((int)pins.rx) + "  TX: GPIO " + String((int)pins.tx));
    if (logFile) padprintln("Log: " + logPath);
    padprintln("");
    const int statsY = tft.getCursorY();
    padprintln("");
    padprintln("");
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    padprintln("Press ESC to exit bridge");

    uint8_t buf[256];
    uint32_t txBytes = 0;
    uint32_t rxBytes = 0;
    uint32_t lastDrawMs = 0;
    uint32_t lastFlushMs = millis();

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();
        bool active = false;

        // Host USB -> Target UART
        int availUsb = Serial.available();
        if (availUsb > 0) {
            size_t toRead = (size_t)availUsb > sizeof(buf) ? sizeof(buf) : (size_t)availUsb;
            size_t n = Serial.readBytes(buf, toRead);
            if (n > 0) {
                Serial1.write(buf, n);
                txBytes += n;
                active = true;
            }
        }

        // Target UART -> Host USB
        int availUart = Serial1.available();
        if (availUart > 0) {
            size_t toRead = (size_t)availUart > sizeof(buf) ? sizeof(buf) : (size_t)availUart;
            size_t n = Serial1.readBytes(buf, toRead);
            if (n > 0) {
                Serial.write(buf, n);
                if (logFile) logFile.write(buf, n);
                rxBytes += n;
                active = true;
            }
        }

        uint32_t now = millis();
        if (now - lastDrawMs >= 200) {
            lastDrawMs = now;
            tft.fillRect(BORDER_PAD_X, statsY, tftWidth - 2 * BORDER_PAD_X, LH * 2 + 4, bruceConfig.bgColor);
            tft.setTextSize(FP);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            char statBuf[48];
            snprintf(statBuf, sizeof(statBuf), "TX: %lu B   RX: %lu B", (unsigned long)txBytes, (unsigned long)rxBytes);
            tft.drawString(statBuf, BORDER_PAD_X, statsY);
        }

        if (logFile && (now - lastFlushMs >= 1000)) {
            logFile.flush();
            lastFlushMs = now;
        }

        if (!active) vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    Serial1.end();
    if (serialcmdsTaskHandle != nullptr) vTaskResume(serialcmdsTaskHandle);
    check(EscPress);
}

/*
 * Live interactive terminal on Serial1:
 *   - partial-line prompts (login:, =>, #) flushed after a short idle gap
 *   - ANSI escape stripping, ASCII/HEX view, HEX TX, SD logging
 *   - quick action / macro menu via SEL on every board, including T-Deck
 */
static void uart_terminal_run() {
    if (wireUartActive) {
        displayError("UART busy (serial cmd)", true);
        return;
    }
    if (USBserial.getSerialOutput() == &Serial1) {
        displayError("(E) UART already in use", true);
        return;
    }

    BruceConfigPins::UARTPins pins = bruceConfigPins.uart_bus;
    if (pins.rx < 0 || pins.tx < 0) {
        displayError("Set RX/TX pins first", true);
        return;
    }

    Serial1.end();
    Serial1.setRxBufferSize(4096); // ride out screen redraws at high baudrates
    Serial1.begin(uartBaud, framePresets[uartFrameIdx].config, pins.rx, pins.tx, uartInvert);

    File logFile;
    String logPath;
    if (uartLogToSd) logFile = openUartLogFile(logPath);

    auto drawHeader = [&]() {
        drawMainBorderWithTitle("UART Terminal");
        tft.setTextSize(FP);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        char hdr[64];
        snprintf(
            hdr,
            sizeof(hdr),
            "%lu %s R%d T%d %s%s",
            (unsigned long)uartBaud,
            framePresets[uartFrameIdx].label,
            (int)pins.rx,
            (int)pins.tx,
            uartHexView ? "HEX" : "ASC",
            logFile ? " LOG" : ""
        );
        tft.drawString(hdr, BORDER_PAD_X, uiContentTop());
    };

    drawHeader();

    // Last text row inside the border is reserved for the input/status line
    const int16_t inputY = uiFooterTop();
    const int16_t areaY = uiContentTop() + LH + 4;
    const int16_t areaH = max((int16_t)LH, (int16_t)(inputY - 2 - areaY));
    ScrollableTextArea area(FP, BORDER_PAD_X, areaY, tftWidth - 2 * BORDER_PAD_X, areaH, false, false);
    const int16_t inputMaxChars = max((int16_t)8, (int16_t)((tftWidth - 2 * BORDER_PAD_X) / LW - 2));

    String input = "";
    input.reserve(UART_TERM_INPUT_LEN + 2);
    String rxRun = "";
    rxRun.reserve(UART_TERM_FLUSH_LEN + 4);
    AnsiStripper ansi;

    uint8_t hexBuf[8];
    size_t hexCount = 0;
    uint32_t hexOffset = 0;
    bool hasLivePromptLine = false;
    uint32_t lastRxByteMs = 0;
    uint32_t lastLogFlushMs = millis();
    bool inputDirty = true;

    auto boundHistory = [&]() {
        if (area.getMaxLines() > UART_TERM_MAX_LINES) {
            size_t extra = area.getMaxLines() - UART_TERM_MAX_LINES;
            area.linesBuffer.erase(area.linesBuffer.begin(), area.linesBuffer.begin() + extra);
            area.firstVisibleLine = (area.firstVisibleLine > extra) ? (area.firstVisibleLine - extra) : 0;
            area.lastVisibleLine = (area.lastVisibleLine > extra) ? (area.lastVisibleLine - extra) : 0;
        }
    };

    auto commitPartialLine = [&]() {
        if (hasLivePromptLine && !area.linesBuffer.empty()) {
            area.linesBuffer.back() = rxRun;
        } else {
            area.addLine(rxRun);
        }
        hasLivePromptLine = false;
        rxRun = "";
    };

    auto updateLivePromptLine = [&]() {
        if (rxRun.isEmpty()) return;
        if (hasLivePromptLine && !area.linesBuffer.empty()) {
            area.linesBuffer.back() = rxRun;
        } else {
            area.addLine(rxRun);
            hasLivePromptLine = true;
        }
    };

    auto drawInputLine = [&]() {
        tft.fillRect(BORDER_PAD_X, inputY, tftWidth - 2 * BORDER_PAD_X, LH, bruceConfig.bgColor);
        tft.setTextSize(FP);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
#ifdef HAS_KEYBOARD
        String view = input;
        if (view.length() > (size_t)inputMaxChars) view = view.substring(view.length() - inputMaxChars);
        tft.drawString(">" + view + "_", BORDER_PAD_X, inputY);
#else
        tft.drawString("[OK] Actions  [ESC] Exit", BORDER_PAD_X, inputY);
#endif
        inputDirty = false;
    };

    auto sendLine = [&]() {
        Serial1.print(input);
        sendLineEnding(Serial1, uartLineEnding);
        if (uartLocalEcho) {
            if (hasLivePromptLine && !area.linesBuffer.empty()) {
                area.linesBuffer.back() += input;
                hasLivePromptLine = false;
                rxRun = "";
            } else {
                area.addLine("> " + input);
            }
            boundHistory();
            if (area.getMaxLines() > area.getMaxVisibleLines()) area.scrollToLine(area.getMaxLines());
            area.draw(true);
        }
        if (logFile) {
            logFile.print("> ");
            logFile.println(input);
        }
        input = "";
        inputDirty = true;
    };

    auto openQuickActions = [&]() {
        bool doSendHex = false;
        bool doSendText = false;
        uint8_t ctrlByte = 0;
        bool doBreak = false;
        bool doClear = false;
        bool doToggleHex = false;
        bool doToggleLog = false;
        bool doLineEnd = false;

        std::vector<Option> actOpts = {
#ifndef HAS_KEYBOARD
            {"Type & Send Text", [&]() { doSendText = true; }},
#endif
            {"Send HEX Bytes",   [&]() { doSendHex = true; } },
            {"Send Ctrl+C (03)", [&]() { ctrlByte = 0x03; }  },
            {"Send Ctrl+D (04)", [&]() { ctrlByte = 0x04; }  },
            {"Send Ctrl+Z (1A)", [&]() { ctrlByte = 0x1A; }  },
            {"Send Break (250ms)", [&]() { doBreak = true; } },
            {String("View: ") + (uartHexView ? "HEX -> ASCII" : "ASCII -> HEX"),
             [&]() { doToggleHex = true; }                   },
            {String("SD Log: ") + (logFile ? "Stop" : "Start"),
             [&]() { doToggleLog = true; }                   },
            {String("EOL: ") + lineEndingLabel(uartLineEnding),
             [&]() { doLineEnd = true; }                     },
            {"Clear Buffer",     [&]() { doClear = true; }   },
            {"Resume Terminal",  []() {}                     },
        };
        loopOptions(actOpts, MENU_TYPE_SUBMENU, "UART Actions");

        if (doSendText) {
            String msg = keyboard(input, UART_TERM_INPUT_LEN, "Send to UART:");
            if (msg != "\x1B") {
                input = msg;
                sendLine();
            }
        } else if (doSendHex) {
            String hexStr = hex_keyboard("", 64, "Send HEX bytes:");
            if (hexStr != "\x1B" && !hexStr.isEmpty()) {
                uint8_t raw[64];
                int n = parseHexBytes(hexStr, raw, sizeof(raw));
                if (n > 0) {
                    Serial1.write(raw, (size_t)n);
                    area.addLine("TX HEX: " + hexStr);
                } else {
                    displayError("Invalid HEX pairs", true);
                }
            }
        } else if (ctrlByte != 0) {
            Serial1.write(ctrlByte);
            char tag[24];
            snprintf(tag, sizeof(tag), "[TX 0x%02X]", ctrlByte);
            area.addLine(tag);
        } else if (doBreak) {
            Serial1.end();
            pinMode(pins.tx, OUTPUT);
            digitalWrite(pins.tx, uartInvert ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(250));
            Serial1.begin(uartBaud, framePresets[uartFrameIdx].config, pins.rx, pins.tx, uartInvert);
            area.addLine("[TX BREAK 250ms]");
        } else if (doToggleHex) {
            uartHexView = !uartHexView;
            hexCount = 0;
            hasLivePromptLine = false;
            rxRun = "";
        } else if (doToggleLog) {
            if (logFile) {
                logFile.flush();
                logFile.close();
                uartLogToSd = false;
                area.addLine("[LOG STOPPED]");
            } else {
                logFile = openUartLogFile(logPath);
                uartLogToSd = (bool)logFile;
                area.addLine(logFile ? ("[LOG: " + logPath + "]") : "[LOG FAILED]");
            }
        } else if (doLineEnd) {
            uart_select_line_ending();
        } else if (doClear) {
            area.clear();
            rxRun = "";
            hexCount = 0;
            hexOffset = 0;
            hasLivePromptLine = false;
        }

        // Redraw terminal frame cleanly after returning from modal menu
        drawHeader();
        boundHistory();
        if (area.getMaxLines() > area.getMaxVisibleLines()) area.scrollToLine(area.getMaxLines());
        area.draw(true);
        inputDirty = true;
        check(SelPress);
        check(EscPress);
    };

    check(SelPress);
    check(EscPress);

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();

        // 1) Drain RX bytes from Serial1
        bool gotRx = false;
        bool wasAtBottom = area.lastVisibleLine >= area.getMaxLines();
        while (Serial1.available() > 0) {
            uint8_t b = (uint8_t)Serial1.read();
            if (logFile) logFile.write(b);
            gotRx = true;
            lastRxByteMs = millis();

            if (uartHexView) {
                hexBuf[hexCount++] = b;
                if (hexCount == sizeof(hexBuf)) {
                    area.addLine(formatHexRow(hexOffset, hexBuf, hexCount));
                    hexOffset += hexCount;
                    hexCount = 0;
                }
            } else {
                if (!ansi.feed(b)) continue;
                if (b == '\r') continue;
                if (b == '\b' || b == 0x7F) {
                    if (!rxRun.isEmpty()) rxRun.remove(rxRun.length() - 1);
                    continue;
                }
                if (b == '\n' || rxRun.length() >= UART_TERM_FLUSH_LEN) {
                    commitPartialLine();
                    if (b != '\n' && b >= 32) rxRun += (char)b;
                } else if (b == '\t') {
                    rxRun += "  ";
                } else if (b >= 32 && b <= 126) {
                    rxRun += (char)b;
                }
            }
        }

        // Flush partial prompt (e.g. "login: ", "=> ", "root@router:/# ") after short idle gap
        bool flushedPrompt = false;
        if (!gotRx && lastRxByteMs != 0 && (millis() - lastRxByteMs) >= UART_PROMPT_FLUSH_MS) {
            if (uartHexView && hexCount > 0) {
                area.addLine(formatHexRow(hexOffset, hexBuf, hexCount));
                hexOffset += hexCount;
                hexCount = 0;
                flushedPrompt = true;
            } else if (!uartHexView && !rxRun.isEmpty()) {
                updateLivePromptLine();
                flushedPrompt = true;
            }
            lastRxByteMs = 0;
        }

        if (gotRx || flushedPrompt) {
            boundHistory();
            if (wasAtBottom && area.getMaxLines() > area.getMaxVisibleLines()) {
                area.scrollToLine(area.getMaxLines());
            }
            area.draw(flushedPrompt);
        }

        // 2) Handle user input (Keyboard + Trackball/Sel on T-Deck/Cardputer; Sel menu on 3-btn boards)
        bool sent = false;
#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (key.del) EscPress = false; // Backspace arrives as DEL, not ESC exit
        if (key.pressed) {
            for (auto c : key.word) {
                if (c == '\n' || c == '\r') continue;
                if (c == '\t' || c >= 32) {
                    if (input.length() < UART_TERM_INPUT_LEN) input += c;
                    inputDirty = true;
                }
            }
            if (key.del) {
                if (!input.isEmpty()) input.remove(input.length() - 1);
                inputDirty = true;
            }
            if (key.enter) {
                sent = true;
                SelPress = false; // prevent Enter from also opening quick actions
            }
        }
        // Trackball / Sel click opens Quick Macros (Ctrl+C, HEX, Log, Break, EOL)
        if (!sent && check(SelPress)) { openQuickActions(); }
#else
        if (check(SelPress)) { openQuickActions(); }
#endif
        if (sent) sendLine();

        // 3) Scroll history (Trackball / Encoder / Up-Down)
        if (check(PrevPress) || check(UpPress)) {
            area.scrollUp();
            area.draw();
        } else if (check(NextPress) || check(DownPress)) {
            area.scrollDown();
            area.draw();
        }

        if (inputDirty) drawInputLine();

        if (logFile && (millis() - lastLogFlushMs) >= 1000) {
            logFile.flush();
            lastLogFlushMs = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    area.clear();
    area.linesBuffer.shrink_to_fit();
    Serial1.end();
    check(SelPress);
}

/*
 * Serial-command primitives for `wire uart ...` (see wire_tools.h): an exclusive
 * Serial1 session that makes the UI terminal and bridge refuse to take the port
 * while it is open.
 */
bool wire_uart_begin(uint32_t baud, int8_t rx, int8_t tx) {
    if (baud < UART_BAUD_MIN || baud > UART_BAUD_MAX) return false;
    if (rx < 0) rx = (int8_t)bruceConfigPins.uart_bus.rx;
    if (tx < 0) tx = (int8_t)bruceConfigPins.uart_bus.tx;
    if (rx < 0 || tx < 0) return false;
    if (wireUartActive) wire_uart_end();

    Serial1.end();
    Serial1.setRxBufferSize(4096);
    Serial1.begin(baud, framePresets[uartFrameIdx].config, rx, tx, uartInvert);
    uartBaud = baud;
    wireUartActive = true;
    return true;
}

void wire_uart_end() {
    wireUartActive = false;
    Serial1.end();
}

int wire_uart_send(const String &text) {
    if (!wireUartActive) return -1;
    Serial1.print(text);
    return (int)text.length();
}

int wire_uart_read(String &out, uint32_t timeoutMs) {
    if (!wireUartActive) return -1;
    out = "";
    uint32_t deadline = millis() + timeoutMs;
    while ((int32_t)(millis() - deadline) < 0) {
        while (Serial1.available() > 0) out += (char)Serial1.read();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return (int)out.length();
}

String wire_uart_status() {
    BruceConfigPins::UARTPins pins = bruceConfigPins.uart_bus;
    return String("baud=") + uartBaud + " frame=" + framePresets[uartFrameIdx].label + " rx=" + int(pins.rx) +
           " tx=" + int(pins.tx) + " invert=" + (uartInvert ? "1" : "0") + " active=" +
           (wireUartActive ? "1" : "0");
}

/*
 * Terminal configuration menu available on every board (including T-Deck and
 * T-Deck Plus): terminal and bridge actions, baudrate, pins, framing, line
 * ending, view mode and logging.
 */
void uart_terminal_setup() {
    returnToMenu = false;
    while (!returnToMenu) {
        BruceConfigPins::UARTPins &pins = bruceConfigPins.uart_bus;
        std::vector<Option> localOpts = {
            {"Start Terminal", uart_terminal_run},
            {"USB-UART Bridge", uart_usb_bridge},
            {"Auto-Baud Detect", uart_autobaud_ui},
            {String("Baudrate: ") + uartBaud, uart_select_baudrate},
            {String("Pins: RX ") + int(pins.rx) + " TX " + int(pins.tx),
             []() { setUARTPinsMenu(bruceConfigPins.uart_bus); }},
            {"Swap RX <-> TX",
             []() {
                 gpio_num_t tmp = bruceConfigPins.uart_bus.rx;
                 bruceConfigPins.uart_bus.rx = bruceConfigPins.uart_bus.tx;
                 bruceConfigPins.uart_bus.tx = tmp;
                 displaySuccess(
                     "RX:" + String((int)bruceConfigPins.uart_bus.rx) +
                         " TX:" + String((int)bruceConfigPins.uart_bus.tx),
                     true
                 );
             }},
#ifdef T_DECK_PLUS
            {"Preset: T-Deck+ GPS (38400)",
             []() {
                 bruceConfigPins.uart_bus = {(gpio_num_t)44, (gpio_num_t)43};
                 uartBaud = 38400;
                 uartFrameIdx = 0;
                 uartInvert = false;
                 displaySuccess("T-Deck+ GPS 38400 (RX44/TX43)", true);
             }},
#endif
            {String("Framing: ") + framePresets[uartFrameIdx].label, uart_select_framing},
            {String("Line Ending: ") + lineEndingLabel(uartLineEnding), uart_select_line_ending},
            {String("View: ") + (uartHexView ? "HEX" : "ASCII"),
             []() { uartHexView = !uartHexView; }},
            {String("Local Echo: ") + (uartLocalEcho ? "ON" : "OFF"),
             []() { uartLocalEcho = !uartLocalEcho; }},
            {String("Invert Signal: ") + (uartInvert ? "ON" : "OFF"),
             []() { uartInvert = !uartInvert; }},
            {String("Log to SD: ") + (uartLogToSd ? "ON" : "OFF"),
             []() { uartLogToSd = !uartLogToSd; }},
            {"Main Menu", []() { backToMenu(); }},
        };
        loopOptions(localOpts, MENU_TYPE_SUBMENU, "UART Terminal");
    }
}
#endif
