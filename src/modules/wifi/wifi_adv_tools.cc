#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "wifi_adv_tools.h"

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
} // namespace bruce_i18n
#endif
#if __has_include("core/location_state.h")
#include "core/location_state.h"
#define WIFI_ADV_HAS_LOC 1
#endif
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include "modules/others/audio.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <algorithm>
#include <esp_wifi.h>
#include <globals.h>
#include <vector>

namespace {

static String fmtMac(const uint8_t m[6]) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

static FS *pickWritableFs() {
    if (setupSdCard()) return &SD;
    if (checkLittleFsSize()) return &LittleFS;
    return nullptr;
}

static void getLocCoords(double &lat, double &lon) {
    lat = 0.0;
    lon = 0.0;
#if defined(WIFI_ADV_HAS_LOC)
    bruceLocation.ensureLoaded();
    if (bruceLocation.hasLocation()) {
        lat = bruceLocation.lat();
        lon = bruceLocation.lon();
    }
#endif
}

/*
 * Passive Wi-Fi probe-request monitor (privacy and RF client diagnostics):
 * registers a promiscuous RX callback, parses 802.11 probe-request management
 * frames, and extracts the STA MAC, requested SSID, and supported rates.
 */
constexpr size_t PROBE_MAX_ENTRIES = 40;

struct ProbeEntry {
    bool active = false;
    uint8_t mac[6] = {0};
    char ssid[33] = {0};
    int8_t rssi = -100;
    uint8_t channel = 0;
    uint16_t count = 0;
    uint32_t lastMs = 0;
};

struct ProbeCtx {
    ProbeEntry *items = nullptr;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint32_t totalProbes = 0;
};

static ProbeCtx *s_probeCtx = nullptr;

static void probeRxCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || !buf || !s_probeCtx || !s_probeCtx->items) return;
    const wifi_promiscuous_pkt_t *pkt = static_cast<const wifi_promiscuous_pkt_t *>(buf);
    const uint8_t *p = pkt->payload;
    uint32_t len = pkt->rx_ctrl.sig_len;
    if (len < 28) return;

    // 802.11 Management frame: subtype 0x04 = Probe Request
    uint8_t fc0 = p[0];
    if ((fc0 & 0x0C) != 0x00 || ((fc0 >> 4) & 0x0F) != 0x04) return;

    const uint8_t *sa = p + 10;
    // Tagged parameters begin at byte 24; Tag 0 = SSID
    char ssid[33] = {0};
    if (p[24] == 0x00) {
        uint8_t slen = p[25];
        if (slen > 0 && slen <= 32 && (uint32_t)(26 + slen) <= len) {
            memcpy(ssid, p + 26, slen);
            ssid[slen] = '\0';
        }
    }

    uint32_t now = millis();
    portENTER_CRITICAL(&s_probeCtx->mux);
    s_probeCtx->totalProbes++;
    int match = -1, empty = -1, oldest = 0;
    uint32_t oldestMs = 0xFFFFFFFFu;
    for (size_t i = 0; i < PROBE_MAX_ENTRIES; ++i) {
        ProbeEntry &e = s_probeCtx->items[i];
        if (e.active) {
            if (memcmp(e.mac, sa, 6) == 0 && strcmp(e.ssid, ssid) == 0) {
                match = (int)i;
                break;
            }
            if (e.lastMs < oldestMs) {
                oldestMs = e.lastMs;
                oldest = (int)i;
            }
        } else if (empty < 0) {
            empty = (int)i;
        }
    }
    int idx = (match >= 0) ? match : ((empty >= 0) ? empty : oldest);
    ProbeEntry &slot = s_probeCtx->items[idx];
    if (match >= 0) {
        slot.rssi = (int8_t)pkt->rx_ctrl.rssi;
        slot.channel = pkt->rx_ctrl.channel;
        if (slot.count < 65535) slot.count++;
        slot.lastMs = now;
    } else {
        slot.active = true;
        memcpy(slot.mac, sa, 6);
        strncpy(slot.ssid, ssid, 32);
        slot.ssid[32] = '\0';
        slot.rssi = (int8_t)pkt->rx_ctrl.rssi;
        slot.channel = pkt->rx_ctrl.channel;
        slot.count = 1;
        slot.lastMs = now;
    }
    portEXIT_CRITICAL(&s_probeCtx->mux);
}

/*
 * Defensive Wi-Fi IDS watchdog: tallies deauthentication, disassociation, and
 * beacon frames per source to detect flood attacks, raising an alert when a
 * station's frame rate exceeds the configured threshold.
 */
constexpr size_t IDS_MAX_LOGS = 24;

struct IdsEvent {
    uint32_t tsMs;
    char tag[10];
    char detail[38];
    bool critical;
};

struct IdsCtx {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint32_t deauthTotal = 0;
    uint32_t disassocTotal = 0;
    uint32_t deauthWindow = 0;
    uint32_t beaconWindow = 0;
    uint8_t lastSrc[6] = {0};
    uint8_t lastReason = 0;
    bool newAlert = false;
};

static IdsCtx *s_idsCtx = nullptr;

static void idsRxCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || !buf || !s_idsCtx) return;
    const wifi_promiscuous_pkt_t *pkt = static_cast<const wifi_promiscuous_pkt_t *>(buf);
    const uint8_t *p = pkt->payload;
    uint32_t len = pkt->rx_ctrl.sig_len;
    if (len < 26) return;

    uint8_t fc0 = p[0];
    if ((fc0 & 0x0C) != 0x00) return;
    uint8_t sub = (fc0 >> 4) & 0x0F;

    portENTER_CRITICAL(&s_idsCtx->mux);
    if (sub == 0x0C) { // Deauthentication
        s_idsCtx->deauthTotal++;
        s_idsCtx->deauthWindow++;
        memcpy(s_idsCtx->lastSrc, p + 10, 6);
        s_idsCtx->lastReason = p[24];
        s_idsCtx->newAlert = true;
    } else if (sub == 0x0A) { // Disassociation
        s_idsCtx->disassocTotal++;
        s_idsCtx->deauthWindow++;
        memcpy(s_idsCtx->lastSrc, p + 10, 6);
        s_idsCtx->lastReason = p[24];
        s_idsCtx->newAlert = true;
    } else if (sub == 0x08) { // Beacon
        s_idsCtx->beaconWindow++;
    }
    portEXIT_CRITICAL(&s_idsCtx->mux);
}

static const char *authLabel(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
        default: return "OTHER";
    }
}

} // namespace

void wifi_probe_sniffer() {
    if (!radioHasMemForWifi()) {
        displayError("Low RAM for WiFi", true);
        return;
    }

    ProbeCtx ctx;
    ctx.items = static_cast<ProbeEntry *>(
        psramFound() ? ps_calloc(PROBE_MAX_ENTRIES, sizeof(ProbeEntry))
                     : calloc(PROBE_MAX_ENTRIES, sizeof(ProbeEntry))
    );
    if (!ctx.items) {
        displayError("Out of memory", true);
        return;
    }
    s_probeCtx = &ctx;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(60));
    wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(probeRxCallback);
    esp_wifi_set_promiscuous(true);

    bool namedOnly = true;
    bool paused = false;
    uint8_t ch = 1;
    int scroll = 0;
    uint32_t lastHop = 0, lastDraw = 0;
    bool exitApp = false;

    auto drawHeader = [&]() {
        drawMainBorderWithTitle("Probe Monitor");
        printCenterFootnote("UP/DN=Scroll  SEL=Menu  ESC=Back");
    };
    drawHeader();

    const int top = uiContentTop();
    const int bot = uiContentBottom(true);
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;

    check(SelPress);
    check(EscPress);
    check(UpPress);
    check(DownPress);

    while (!exitApp && !returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();

        if (!paused && now - lastHop >= 350) {
            ch = (ch % 13) + 1;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }

        if (now - lastDraw >= 350) {
            std::vector<ProbeEntry> list;
            list.reserve(PROBE_MAX_ENTRIES); // no heap allocation inside the critical section
            uint32_t total = 0;
            portENTER_CRITICAL(&ctx.mux);
            total = ctx.totalProbes;
            for (size_t i = 0; i < PROBE_MAX_ENTRIES; ++i) {
                if (!ctx.items[i].active) continue;
                if (namedOnly && ctx.items[i].ssid[0] == '\0') continue;
                list.push_back(ctx.items[i]);
            }
            portEXIT_CRITICAL(&ctx.mux);

            std::sort(list.begin(), list.end(), [](const ProbeEntry &a, const ProbeEntry &b) {
                return a.lastMs > b.lastMs;
            });

            tft.fillRect(left, top, w, bot - top, bruceConfig.bgColor);
            tft.setTextSize(1);
            tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
            String hdr = "CH:" + String(ch) + "  PROBES:" + String(total) + "  UNIQ:" + String(list.size()) +
                         (paused ? " [PAUSE]" : "");
            tft.drawString(hdr, left, top, 1);

            int y = top + LH + 4;
            int rowH = LH + 3;
            int maxRows = max(1, (bot - y) / rowH);
            if (scroll > max(0, (int)list.size() - maxRows)) scroll = max(0, (int)list.size() - maxRows);

            if (list.empty()) {
                tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
                tft.drawCentreString("Listening for probe requests...", tftWidth / 2, y + 18, 1);
            } else {
                int maxSsidChars = max(8, (w - 110) / LW);
                for (int r = 0; r < maxRows && (scroll + r) < (int)list.size(); ++r) {
                    const ProbeEntry &e = list[scroll + r];
                    tft.setTextColor(TFT_LIGHTGREY, bruceConfig.bgColor);
                    tft.drawString(fmtMac(e.mac).substring(9), left, y, 1);
                    tft.setTextColor(e.ssid[0] ? TFT_GREEN : TFT_DARKGREY, bruceConfig.bgColor);
                    String sn = e.ssid[0] ? String(e.ssid) : "<broadcast>";
                    if ((int)sn.length() > maxSsidChars) sn = sn.substring(0, maxSsidChars - 1) + "~";
                    tft.drawString(sn, left + 54, y, 1);
                    tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
                    tft.drawRightString(String(e.rssi) + "dB", left + w - 2, y, 1);
                    y += rowH;
                }
            }
            lastDraw = now;
        }

        if (check(EscPress)) exitApp = true;
        else if (check(UpPress) || check(PrevPress)) {
            if (scroll > 0) --scroll;
            lastDraw = 0;
        } else if (check(DownPress) || check(NextPress)) {
            ++scroll;
            lastDraw = 0;
        } else if (check(SelPress)) {
            int act = 0;
            std::vector<Option> m = {
                {paused ? "Resume" : "Pause", [&]() { act = 1; }},
                {namedOnly ? "Show Broadcasts" : "Named SSIDs Only", [&]() { act = 2; }},
                {"Export CSV", [&]() { act = 3; }},
                {"Clear List", [&]() { act = 4; }},
                {"Exit", [&]() { act = 5; }},
            };
            loopOptions(m, MENU_TYPE_SUBMENU, "Probe Monitor");
            if (act == 1) paused = !paused;
            else if (act == 2) {
                namedOnly = !namedOnly;
                scroll = 0;
            } else if (act == 3) {
                FS *fs = pickWritableFs();
                if (fs) {
                    double lat = 0, lon = 0;
                    getLocCoords(lat, lon);
                    File f = fs->open("/probe_log.csv", FILE_WRITE);
                    if (f) {
                        f.println("mac,ssid,rssi,ch,count,lat,lon");
                        // Snapshot under the spinlock; never do SD I/O or heap
                        // allocation with interrupts disabled.
                        ProbeEntry snapshot[PROBE_MAX_ENTRIES];
                        portENTER_CRITICAL(&ctx.mux);
                        memcpy(snapshot, ctx.items, sizeof(snapshot));
                        portEXIT_CRITICAL(&ctx.mux);
                        for (size_t i = 0; i < PROBE_MAX_ENTRIES; ++i) {
                            if (!snapshot[i].active) continue;
                            f.printf(
                                "%s,\"%s\",%d,%u,%u,%.6f,%.6f\n",
                                fmtMac(snapshot[i].mac).c_str(),
                                snapshot[i].ssid,
                                (int)snapshot[i].rssi,
                                (unsigned)snapshot[i].channel,
                                (unsigned)snapshot[i].count,
                                lat,
                                lon
                            );
                        }
                        f.close();
                        displaySuccess("Saved /probe_log.csv", true);
                    }
                } else {
                    displayError("No storage found", true);
                }
            } else if (act == 4) {
                portENTER_CRITICAL(&ctx.mux);
                memset(ctx.items, 0, sizeof(ProbeEntry) * PROBE_MAX_ENTRIES);
                ctx.totalProbes = 0;
                portEXIT_CRITICAL(&ctx.mux);
                scroll = 0;
            } else if (act == 5) {
                exitApp = true;
            }
            if (!exitApp) {
                drawHeader();
                lastDraw = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    s_probeCtx = nullptr;
    free(ctx.items);
    check(SelPress);
    check(EscPress);
}

void wifi_port_scanner() {
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA)) return;
    }

    String defaultIp = WiFi.gatewayIP().toString();
    String targetIp = keyboard(defaultIp, 24, "Target IP (LAN)");
    if (targetIp.isEmpty() || targetIp == "\x1B") return;

    struct PortSpec {
        uint16_t port;
        const char *svc;
        bool isHttp;
    };
    static const PortSpec kPorts[] = {
        {21,   "FTP",    false}, {22,   "SSH",    false}, {23,   "Telnet", false},
        {25,   "SMTP",   false}, {53,   "DNS",    false}, {80,   "HTTP",   true },
        {110,  "POP3",   false}, {139,  "NetBIOS",false}, {143,  "IMAP",   false},
        {443,  "HTTPS",  false}, {445,  "SMB",    false}, {554,  "RTSP",   false},
        {1883, "MQTT",   false}, {3306, "MySQL",  false}, {3389, "RDP",    false},
        {5900, "VNC",    false}, {8080, "HTTP-A", true }, {8443, "HTTPS-A",false},
    };
    constexpr size_t kPortCnt = sizeof(kPorts) / sizeof(kPorts[0]);

    drawMainBorderWithTitle("Port & Banner Audit");
    String report = "Target: " + targetIp + "\nLocal: " + WiFi.localIP().toString() + "\n---\n";
    int openCount = 0;

    int top = BORDER_PAD_Y + FM * LH + 8;
    for (size_t i = 0; i < kPortCnt && !check(EscPress); ++i) {
        wakeUpScreen();
        tft.fillRect(BORDER_PAD_X + 4, top, tftWidth - 2 * BORDER_PAD_X - 8, 36, bruceConfig.bgColor);
        tft.setTextSize(FP);
        tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
        tft.drawString(
            "Scanning " + targetIp + ":" + String(kPorts[i].port) + " (" + kPorts[i].svc + ")",
            BORDER_PAD_X + 6,
            top,
            1
        );
        int barW = tftWidth - 2 * BORDER_PAD_X - 16;
        tft.drawRect(BORDER_PAD_X + 6, top + LH + 6, barW, 8, bruceConfig.secColor);
        int fill = (int)(((i + 1) * (barW - 2)) / kPortCnt);
        if (fill > 0) tft.fillRect(BORDER_PAD_X + 7, top + LH + 7, fill, 6, TFT_GREEN);

        WiFiClient client;
        if (client.connect(targetIp.c_str(), kPorts[i].port, 280)) {
            ++openCount;
            String banner = "";
            if (kPorts[i].isHttp) {
                client.print(
                    "GET / HTTP/1.0\r\nHost: " + targetIp + "\r\nUser-Agent: NezhaAudit/1.0\r\n\r\n"
                );
            }
            uint32_t t0 = millis();
            while (millis() - t0 < 280 && banner.length() < 96) {
                while (client.available() && banner.length() < 96) {
                    char c = (char)client.read();
                    if (c >= 32 && c <= 126) banner += c;
                    else if (c == '\n' && !banner.isEmpty()) break;
                }
                if (!banner.isEmpty()) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            client.stop();
            banner.trim();
            report += String(kPorts[i].port) + "/tcp OPEN [" + kPorts[i].svc + "]";
            if (!banner.isEmpty()) report += "\n  " + banner.substring(0, 64);
            report += "\n";
        }
    }

    if (openCount == 0) report += "No open ports in top-18 list.\n";
    report += "---\nOpen: " + String(openCount) + " / " + String((unsigned)kPortCnt) + "\n";

    // Save report if storage available
    FS *fs = pickWritableFs();
    if (fs) {
        File f = fs->open("/net_recon.txt", FILE_WRITE);
        if (f) {
            f.print(report);
            f.close();
            report += "Saved -> /net_recon.txt\n";
        }
    }

    ScrollableTextArea area("Port Scan Result");
    area.fromString(report);
    area.show(true);
}

void wifi_ids_watchdog() {
    if (!radioHasMemForWifi()) {
        displayError("Low RAM for WiFi", true);
        return;
    }

    IdsCtx ctx;
    s_idsCtx = &ctx;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(60));
    wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(idsRxCallback);
    esp_wifi_set_promiscuous(true);

    std::vector<IdsEvent> events;
    uint8_t ch = 1;
    bool chLock = false;
    bool audioAlert = true;
    uint32_t lastHop = 0, lastWindow = millis(), lastDraw = 0;
    bool exitApp = false;

    auto drawFrame = [&]() {
        drawMainBorderWithTitle("WiFi IDS Watchdog");
        printCenterFootnote("UP/DN=CH Lock  SEL=Menu  ESC=Back");
    };
    drawFrame();

    const int top = uiContentTop();
    const int bot = uiContentBottom(true);
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;

    auto pushEvent = [&](const char *tag, const String &detail, bool crit) {
        IdsEvent ev;
        ev.tsMs = millis() / 1000u;
        strncpy(ev.tag, tag, sizeof(ev.tag) - 1);
        ev.tag[sizeof(ev.tag) - 1] = '\0';
        strncpy(ev.detail, detail.c_str(), sizeof(ev.detail) - 1);
        ev.detail[sizeof(ev.detail) - 1] = '\0';
        ev.critical = crit;
        events.insert(events.begin(), ev);
        if (events.size() > IDS_MAX_LOGS) events.pop_back();
        if (audioAlert && bruceConfig.soundEnabled) {
            _tone(crit ? 2800 : 1800, crit ? 70 : 35, PLAYBACK_ASYNC);
        }
    };

    check(SelPress);
    check(EscPress);
    check(UpPress);
    check(DownPress);

    while (!exitApp && !returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();

        if (!chLock && now - lastHop >= 300) {
            ch = (ch % 13) + 1;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }

        // Evaluate 1-second attack detection window
        if (now - lastWindow >= 1000) {
            lastWindow = now;
            uint32_t dw = 0, bw = 0;
            uint8_t src[6] = {0};
            uint8_t rsn = 0;
            bool alert = false;
            portENTER_CRITICAL(&ctx.mux);
            dw = ctx.deauthWindow;
            bw = ctx.beaconWindow;
            memcpy(src, ctx.lastSrc, 6);
            rsn = ctx.lastReason;
            alert = ctx.newAlert;
            ctx.deauthWindow = 0;
            ctx.beaconWindow = 0;
            ctx.newAlert = false;
            portEXIT_CRITICAL(&ctx.mux);

            if (dw >= 4) {
                pushEvent("DEAUTH!", "CH" + String(ch) + " " + fmtMac(src).substring(9) + " x" + String(dw) + " r" + String(rsn), true);
                lastDraw = 0;
            } else if (alert && dw > 0) {
                pushEvent("DEAUTH", "CH" + String(ch) + " " + fmtMac(src).substring(9) + " r" + String(rsn), false);
                lastDraw = 0;
            }
            if (bw >= 45) {
                pushEvent("FLOOD!", "Beacon burst CH" + String(ch) + " (" + String(bw) + "/s)", true);
                lastDraw = 0;
            }
        }

        if (now - lastDraw >= 450) {
            uint32_t dt = 0, ds = 0;
            portENTER_CRITICAL(&ctx.mux);
            dt = ctx.deauthTotal;
            ds = ctx.disassocTotal;
            portEXIT_CRITICAL(&ctx.mux);

            tft.fillRect(left, top, w, bot - top, bruceConfig.bgColor);
            tft.setTextSize(1);
            tft.setTextColor((dt > 0 || ds > 0) ? TFT_RED : TFT_GREEN, bruceConfig.bgColor);
            String st = "CH:" + String(ch) + (chLock ? "[L]" : "[H]") + "  DEAUTH:" + String(dt) +
                        "  DIS:" + String(ds);
            tft.drawString(st, left, top, 1);

            int y = top + LH + 4;
            tft.drawFastHLine(left, y, w, bruceConfig.secColor);
            y += 4;

            if (events.empty()) {
                tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
                tft.drawCentreString("Airspace clean - monitoring...", tftWidth / 2, y + 18, 1);
            } else {
                int rowH = LH + 3;
                int maxRows = max(1, (bot - y) / rowH);
                for (int i = 0; i < (int)events.size() && i < maxRows; ++i) {
                    const IdsEvent &ev = events[i];
                    tft.setTextColor(ev.critical ? TFT_RED : TFT_YELLOW, bruceConfig.bgColor);
                    tft.drawString(ev.tag, left, y, 1);
                    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
                    tft.drawString(ev.detail, left + 48, y, 1);
                    y += rowH;
                }
            }
            lastDraw = now;
        }

        if (check(EscPress)) exitApp = true;
        else if (check(UpPress) || check(NextPress)) {
            chLock = true;
            ch = (ch % 13) + 1;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastDraw = 0;
        } else if (check(DownPress) || check(PrevPress)) {
            chLock = !chLock;
            lastDraw = 0;
        } else if (check(SelPress)) {
            int act = 0;
            std::vector<Option> m = {
                {chLock ? "Unlock Channel Hop" : "Lock Current CH", [&]() { act = 1; }},
                {audioAlert ? "Alert Beep: ON" : "Alert Beep: OFF", [&]() { act = 2; }},
                {"Save IDS Log", [&]() { act = 3; }},
                {"Clear Alerts", [&]() { act = 4; }},
                {"Exit", [&]() { act = 5; }},
            };
            loopOptions(m, MENU_TYPE_SUBMENU, "WiFi IDS");
            if (act == 1) chLock = !chLock;
            else if (act == 2) audioAlert = !audioAlert;
            else if (act == 3) {
                FS *fs = pickWritableFs();
                if (fs) {
                    double lat = 0, lon = 0;
                    getLocCoords(lat, lon);
                    File f = fs->open("/wifi_ids.log", FILE_APPEND);
                    if (f) {
                        f.printf("=== IDS Snapshot (%.6f, %.6f) ===\n", lat, lon);
                        for (const auto &ev : events) f.printf("[%lus] %s %s\n", (unsigned long)ev.tsMs, ev.tag, ev.detail);
                        f.close();
                        displaySuccess("Saved /wifi_ids.log", true);
                    }
                } else {
                    displayError("No storage found", true);
                }
            } else if (act == 4) {
                events.clear();
                portENTER_CRITICAL(&ctx.mux);
                ctx.deauthTotal = ctx.disassocTotal = 0;
                portEXIT_CRITICAL(&ctx.mux);
            } else if (act == 5) {
                exitApp = true;
            }
            if (!exitApp) {
                drawFrame();
                lastDraw = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    s_idsCtx = nullptr;
    check(SelPress);
    check(EscPress);
}

void wifi_sec_auditor() {
    if (!radioHasMemForWifi()) {
        displayError("Low RAM for WiFi", true);
        return;
    }
    displayInfo("Auditing nearby WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(80));
    int16_t n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        WiFi.scanDelete();
        displayWarning("No WiFi networks found", true);
        return;
    }

    int openC = 0, wepC = 0, wpaC = 0, wpa2C = 0, wpa3C = 0, hidC = 0;
    String rep = "WiFi Security Audit (" + String(n) + " APs)\n";
    double lat = 0, lon = 0;
    getLocCoords(lat, lon);
    if (fabs(lat) > 0.0001 || fabs(lon) > 0.0001) {
        char gb[36];
        snprintf(gb, sizeof(gb), "GPS: %.5f, %.5f\n", lat, lon);
        rep += gb;
    }
    rep += "---\n";

    FS *fs = pickWritableFs();
    File csv;
    if (fs) {
        csv = fs->open("/wifi_audit.csv", FILE_WRITE);
        if (csv) csv.println("ssid,bssid,ch,rssi,auth,risk,lat,lon");
    }

    for (int16_t i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        wifi_auth_mode_t am = WiFi.encryptionType(i);
        const char *risk = "LOW";
        if (am == WIFI_AUTH_OPEN) {
            ++openC;
            risk = "HIGH";
        } else if (am == WIFI_AUTH_WEP) {
            ++wepC;
            risk = "CRIT";
        } else if (am == WIFI_AUTH_WPA_PSK) {
            ++wpaC;
            risk = "MED";
        } else if (am == WIFI_AUTH_WPA3_PSK || am == WIFI_AUTH_WPA2_WPA3_PSK) {
            ++wpa3C;
            risk = "SAFE";
        } else {
            ++wpa2C;
        }
        if (ssid.isEmpty()) ++hidC;

        String nm = ssid.isEmpty() ? "<hidden>" : ssid;
        rep += "[" + String(risk) + "] " + nm + " (" + authLabel(am) + " C" + String(WiFi.channel(i)) + " " +
               String(WiFi.RSSI(i)) + "dB)\n";
        if (csv) {
            csv.printf(
                "\"%s\",%s,%d,%d,%s,%s,%.6f,%.6f\n",
                nm.c_str(),
                WiFi.BSSIDstr(i).c_str(),
                (int)WiFi.channel(i),
                (int)WiFi.RSSI(i),
                authLabel(am),
                risk,
                lat,
                lon
            );
        }
    }
    if (csv) csv.close();
    WiFi.scanDelete();

    String summary = "WEP:" + String(wepC) + " OPEN:" + String(openC) + " WPA:" + String(wpaC) +
                     " WPA2:" + String(wpa2C) + " WPA3:" + String(wpa3C) + " HID:" + String(hidC) + "\n";
    if (fs) summary += "Saved -> /wifi_audit.csv\n";
    summary += "---\n" + rep;

    ScrollableTextArea area("WiFi Audit");
    area.fromString(summary);
    area.show(true);
}

void wifi_adv_tools_menu() {
    std::vector<Option> localOpts = {
        {"Probe Request Monitor", wifi_probe_sniffer},
        {"LAN Port & Banner Scan", wifi_port_scanner},
        {"WiFi IDS Watchdog",      wifi_ids_watchdog},
        {"WiFi Security Audit",    wifi_sec_auditor },
        {"Back",                   []() {}          },
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "WiFi Adv Tools");
}

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT
