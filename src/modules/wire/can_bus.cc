#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "wire_tools.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include <globals.h>
#include <soc/soc_caps.h>

#if __has_include(<driver/twai.h>) && defined(SOC_TWAI_SUPPORTED) && SOC_TWAI_SUPPORTED
#include <driver/twai.h>
#define NEZHA_HAS_TWAI 1
#endif

#if defined(NEZHA_HAS_TWAI)

struct CanBitratePreset {
    const char *label;
    uint32_t kbps;
};

static const CanBitratePreset canBitrates[] = {
    {"125 kbps", 125 },
    {"250 kbps", 250 },
    {"500 kbps", 500 },
    {"1 Mbps",   1000},
};
static uint8_t canBitrateIdx = 2; // 500 kbps (most automotive OBD-II buses)
static int8_t canTxPin = -1;
static int8_t canRxPin = -1;
static bool canLogToSd = false;

static constexpr size_t CAN_MAX_IDS = 48;

struct CanIdEntry {
    uint32_t id;
    uint8_t dlc;
    bool extd;
    uint8_t data[8];
    uint8_t prev[8];
    uint8_t diffMask;
    uint32_t count;
    uint32_t lastMs;
};

static twai_timing_config_t can_timing_for(uint32_t kbps) {
    switch (kbps) {
        case 125: return TWAI_TIMING_CONFIG_125KBITS();
        case 250: return TWAI_TIMING_CONFIG_250KBITS();
        case 1000: return TWAI_TIMING_CONFIG_1MBITS();
        case 500:
        default: return TWAI_TIMING_CONFIG_500KBITS();
    }
}

static void can_resolve_default_pins() {
    if (canTxPin < 0 || canRxPin < 0) {
        if (bruceConfigPins.uart_bus.tx >= 0 && bruceConfigPins.uart_bus.rx >= 0) {
            canTxPin = (int8_t)bruceConfigPins.uart_bus.tx;
            canRxPin = (int8_t)bruceConfigPins.uart_bus.rx;
        } else if (bruceConfigPins.i2c_bus.sda >= 0 && bruceConfigPins.i2c_bus.scl >= 0) {
            canTxPin = (int8_t)bruceConfigPins.i2c_bus.sda;
            canRxPin = (int8_t)bruceConfigPins.i2c_bus.scl;
        }
    }
}

static bool can_driver_start(twai_mode_t mode) {
    can_resolve_default_pins();
    if (canTxPin < 0 || canRxPin < 0 || canTxPin == canRxPin) {
        displayError("Set CAN TX/RX pins first", true);
        return false;
    }

    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)canTxPin, (gpio_num_t)canRxPin, mode);
    g_cfg.rx_queue_len = 32;
    g_cfg.tx_queue_len = 8;
    twai_timing_config_t t_cfg = can_timing_for(canBitrates[canBitrateIdx].kbps);
    twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_cfg, &t_cfg, &f_cfg) != ESP_OK) {
        displayError("TWAI driver install failed", true);
        return false;
    }
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        displayError("TWAI start failed", true);
        return false;
    }
    return true;
}

static void can_driver_stop() {
    twai_stop();
    twai_driver_uninstall();
}

static File openCanLogFile(String &outPath) {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) return File();
    if (!fs->exists("/NezhaLogs")) fs->mkdir("/NezhaLogs");
    for (uint16_t idx = 0; idx < 1000; ++idx) {
        char path[40];
        snprintf(path, sizeof(path), "/NezhaLogs/can_%03u.log", (unsigned)idx);
        if (!fs->exists(path)) {
            outPath = path;
            return fs->open(path, FILE_WRITE);
        }
    }
    return File();
}

static void can_sniffer_run() {
    if (!can_driver_start(TWAI_MODE_LISTEN_ONLY)) return;

    // Heap-allocate fixed POD table once (small ~1.7 KB) to keep task stack lean
    CanIdEntry *table = (CanIdEntry *)calloc(CAN_MAX_IDS, sizeof(CanIdEntry));
    if (!table) {
        can_driver_stop();
        displayError("Not Enough RAM", true);
        return;
    }

    File logFile;
    String logPath;
    if (canLogToSd) logFile = openCanLogFile(logPath);

    size_t idCount = 0;
    uint32_t totalFrames = 0;
    size_t scrollOffset = 0;
    bool paused = false;
    uint32_t lastDrawMs = 0;
    uint32_t lastFlushMs = millis();

    drawMainBorderWithTitle("CAN Sniffer");
    const int contentY = BORDER_PAD_Y + LH + 4;
    const int rowH = LH + 3;
    const int maxRows = max(1, (int)((tftHeight - BORDER_PAD_X - LH - contentY) / rowH));

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();

        if (!paused) {
            twai_message_t msg;
            int budget = 24;
            while (budget-- > 0 && twai_receive(&msg, 0) == ESP_OK) {
                totalFrames++;
                uint8_t dlc = (msg.data_length_code > 8) ? 8 : msg.data_length_code;
                uint32_t now = millis();

                if (logFile) {
                    char line[64];
                    int pos = snprintf(
                        line, sizeof(line), "(%lu.%03lu) can0 %03lX#", (unsigned long)(now / 1000),
                        (unsigned long)(now % 1000), (unsigned long)msg.identifier
                    );
                    for (uint8_t i = 0; i < dlc && pos + 3 < (int)sizeof(line); ++i) {
                        pos += snprintf(line + pos, sizeof(line) - pos, "%02X", msg.data[i]);
                    }
                    logFile.println(line);
                }

                // Update or insert into bounded POD ID table
                int found = -1;
                for (size_t i = 0; i < idCount; ++i) {
                    if (table[i].id == msg.identifier && table[i].extd == (bool)msg.extd) {
                        found = (int)i;
                        break;
                    }
                }
                if (found >= 0) {
                    CanIdEntry &e = table[found];
                    uint8_t diff = 0;
                    for (uint8_t i = 0; i < dlc; ++i) {
                        if (e.data[i] != msg.data[i]) diff |= (1U << i);
                        e.prev[i] = e.data[i];
                        e.data[i] = msg.data[i];
                    }
                    e.dlc = dlc;
                    e.diffMask = diff;
                    e.count++;
                    e.lastMs = now;
                } else if (idCount < CAN_MAX_IDS) {
                    CanIdEntry &e = table[idCount++];
                    e.id = msg.identifier;
                    e.dlc = dlc;
                    e.extd = (bool)msg.extd;
                    memcpy(e.data, msg.data, dlc);
                    memcpy(e.prev, msg.data, dlc);
                    e.diffMask = 0;
                    e.count = 1;
                    e.lastMs = now;
                }
            }
        }

        if (check(SelPress)) paused = !paused;
        if (check(PrevPress) || check(UpPress)) {
            if (scrollOffset > 0) scrollOffset--;
        } else if (check(NextPress) || check(DownPress)) {
            if (scrollOffset + (size_t)maxRows < idCount) scrollOffset++;
        }

        uint32_t now = millis();
        if (now - lastDrawMs >= 120) {
            lastDrawMs = now;
            tft.fillRect(
                BORDER_PAD_X, contentY, tftWidth - 2 * BORDER_PAD_X, tftHeight - BORDER_PAD_X - contentY,
                bruceConfig.bgColor
            );
            tft.setTextSize(FP);
            tft.setTextColor(paused ? TFT_YELLOW : bruceConfig.priColor, bruceConfig.bgColor);
            char hdr[64];
            snprintf(
                hdr, sizeof(hdr), "%s IDs:%u F:%lu %s%s", canBitrates[canBitrateIdx].label, (unsigned)idCount,
                (unsigned long)totalFrames, paused ? "PAUSE" : "LIVE", logFile ? " LOG" : ""
            );
            tft.drawString(hdr, BORDER_PAD_X, contentY);

            int y = contentY + rowH;
            for (size_t r = 0; r < (size_t)(maxRows - 1) && (scrollOffset + r) < idCount; ++r) {
                const CanIdEntry &e = table[scrollOffset + r];
                char idStr[14];
                snprintf(idStr, sizeof(idStr), e.extd ? "%08lX " : "%03lX ", (unsigned long)e.id);
                tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
                tft.drawString(idStr, BORDER_PAD_X, y);
                int x = BORDER_PAD_X + (int)strlen(idStr) * LW * FP;

                for (uint8_t b = 0; b < e.dlc; ++b) {
                    char hb[4];
                    snprintf(hb, sizeof(hb), "%02X ", e.data[b]);
                    bool changed = ((e.diffMask & (1U << b)) != 0) && (now - e.lastMs < 1000);
                    tft.setTextColor(changed ? bruceConfig.priColor : uiTextMutedColor(), bruceConfig.bgColor);
                    tft.drawString(hb, x, y);
                    x += 3 * LW * FP;
                }
                y += rowH;
            }
        }

        if (logFile && (now - lastFlushMs >= 1000)) {
            logFile.flush();
            lastFlushMs = now;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    free(table);
    can_driver_stop();
    check(EscPress);
}

static bool obd_query_pid(uint8_t mode, uint8_t pid, uint8_t outData[8], uint8_t &outDlc) {
    twai_message_t tx = {};
    tx.identifier = 0x7DF;
    tx.extd = 0;
    tx.data_length_code = 8;
    tx.data[0] = (mode == 0x03) ? 0x01 : 0x02;
    tx.data[1] = mode;
    tx.data[2] = pid;

    if (twai_transmit(&tx, pdMS_TO_TICKS(50)) != ESP_OK) return false;

    uint32_t deadline = millis() + 120;
    twai_message_t rx;
    while (millis() < deadline) {
        if (twai_receive(&rx, pdMS_TO_TICKS(20)) == ESP_OK) {
            if (rx.identifier >= 0x7E8 && rx.identifier <= 0x7EF && rx.data_length_code >= 3 &&
                rx.data[1] == (uint8_t)(mode + 0x40)) {
                if (mode == 0x03 || rx.data[2] == pid) {
                    outDlc = rx.data_length_code;
                    memcpy(outData, rx.data, 8);
                    return true;
                }
            }
        }
    }
    return false;
}

static void can_obd2_live() {
    if (!can_driver_start(TWAI_MODE_NORMAL)) return;

    drawMainBorderWithTitle("OBD-II Live PIDs");
    const int contentY = BORDER_PAD_Y + LH + 6;

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();
        uint8_t d[8] = {};
        uint8_t dlc = 0;

        int rpm = -1, speed = -1, coolant = -999, throttle = -1, load = -1;
        if (obd_query_pid(0x01, 0x0C, d, dlc) && dlc >= 5) rpm = ((d[3] << 8) | d[4]) / 4;
        if (obd_query_pid(0x01, 0x0D, d, dlc) && dlc >= 4) speed = d[3];
        if (obd_query_pid(0x01, 0x05, d, dlc) && dlc >= 4) coolant = (int)d[3] - 40;
        if (obd_query_pid(0x01, 0x11, d, dlc) && dlc >= 4) throttle = (d[3] * 100) / 255;
        if (obd_query_pid(0x01, 0x04, d, dlc) && dlc >= 4) load = (d[3] * 100) / 255;

        tft.fillRect(
            BORDER_PAD_X, contentY, tftWidth - 2 * BORDER_PAD_X, tftHeight - BORDER_PAD_X - contentY,
            bruceConfig.bgColor
        );
        tft.setTextSize(FP);
        tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
        int y = contentY;
        const int step = LH + 5;
        tft.drawString("RPM:      " + (rpm >= 0 ? String(rpm) : "--"), BORDER_PAD_X, y);
        y += step;
        tft.drawString("Speed:    " + (speed >= 0 ? (String(speed) + " km/h") : "--"), BORDER_PAD_X, y);
        y += step;
        tft.drawString("Coolant:  " + (coolant > -999 ? (String(coolant) + " C") : "--"), BORDER_PAD_X, y);
        y += step;
        tft.drawString("Throttle: " + (throttle >= 0 ? (String(throttle) + " %") : "--"), BORDER_PAD_X, y);
        y += step;
        tft.drawString("Eng Load: " + (load >= 0 ? (String(load) + " %") : "--"), BORDER_PAD_X, y);
        y += step + 4;
        tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
        tft.drawString("ESC to exit  (500k std)", BORDER_PAD_X, y);

        vTaskDelay(pdMS_TO_TICKS(250));
    }
    can_driver_stop();
    check(EscPress);
}

static void can_obd2_dtc() {
    if (!can_driver_start(TWAI_MODE_NORMAL)) return;

    uint8_t d[8] = {};
    uint8_t dlc = 0;
    bool ok = obd_query_pid(0x03, 0x00, d, dlc);
    can_driver_stop();

    ScrollableTextArea area("OBD-II Stored DTCs");
    if (!ok) {
        area.addLine("No ECU response on 0x7DF");
        area.addLine("Check transceiver & ignition ON");
    } else {
        static const char prefixChars[4] = {'P', 'C', 'B', 'U'};
        int found = 0;
        for (int i = 2; i + 1 < (int)dlc; i += 2) {
            uint16_t raw = ((uint16_t)d[i] << 8) | d[i + 1];
            if (raw == 0) continue;
            char code[8];
            snprintf(
                code,
                sizeof(code),
                "%c%u%03X",
                prefixChars[(raw >> 14) & 0x03],
                (unsigned)((raw >> 12) & 0x03),
                (unsigned)(raw & 0x0FFF)
            );
            area.addLine(String("DTC: ") + code);
            found++;
        }
        if (found == 0) area.addLine("No stored fault codes (0 DTCs)");
    }
    area.show(true);
    area.clear();
}

static void can_pick_pins() {
    can_resolve_default_pins();
    BruceConfigPins::UARTPins tmp = {(gpio_num_t)canRxPin, (gpio_num_t)canTxPin};
    setUARTPinsMenu(tmp);
    canRxPin = (int8_t)tmp.rx;
    canTxPin = (int8_t)tmp.tx;
}

void can_bus_setup() {
    can_resolve_default_pins();
    returnToMenu = false;
    while (!returnToMenu) {
        std::vector<Option> localOpts = {
            {"CAN Sniffer (Listen)",   can_sniffer_run},
            {"OBD-II Live PIDs",       can_obd2_live  },
            {"OBD-II Read DTCs",       can_obd2_dtc   },
            {String("Bitrate: ") + canBitrates[canBitrateIdx].label,
             []() { canBitrateIdx = (canBitrateIdx + 1) % (sizeof(canBitrates) / sizeof(canBitrates[0])); }},
            {String("Pins: RX ") + int(canRxPin) + " TX " + int(canTxPin), can_pick_pins},
            {String("Log .candump: ") + (canLogToSd ? "ON" : "OFF"),
             []() { canLogToSd = !canLogToSd; }},
            {"Main Menu", []() { backToMenu(); }},
        };
        loopOptions(localOpts, MENU_TYPE_SUBMENU, "CAN Bus / OBD-II");
    }
}

#else

void can_bus_setup() { displayError("CAN TWAI unsupported on SoC", true); }

#endif
#endif
