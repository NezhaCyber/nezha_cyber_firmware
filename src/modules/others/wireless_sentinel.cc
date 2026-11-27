#if !defined(LITE_VERSION)
#include "wireless_sentinel.h"

#include "core/display.h"
#include "core/location_state.h"
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include "modules/rf/rf_utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>
#include <globals.h>

#if __has_include(<NimBLEDevice.h>)
#include <NimBLEDevice.h>
#define SENTINEL_HAS_BLE 1
#endif

static constexpr size_t SENTINEL_MAX_EVENTS = 32;
static constexpr size_t SENTINEL_MAX_APS = 24;
static constexpr size_t SENTINEL_MAX_BLE = 32;
static constexpr uint32_t TRACKER_FOLLOW_MS = 90000; // 90 s across >=3 epochs => FOLLOWING
static constexpr uint8_t TRACKER_FOLLOW_EPOCHS = 3;

enum class ThreatSev : uint8_t { INFO = 0, WATCH = 1, ALERT = 2 };

struct SentinelEvent {
    uint32_t tsSec;
    ThreatSev sev;
    char tag[12];
    char detail[32];
};

struct SentinelAp {
    bool used;
    uint8_t bssid[6];
    char ssid[24];
    bool encrypted;
    bool twinAlerted;
};

struct SentinelBleEntry {
    bool used;
    uint8_t mac[6];
    uint8_t kind; // 1=Tracker, 2=Skimmer
    char label[12];
    uint32_t firstMs;
    uint32_t lastMs;
    uint32_t lastEpoch;
    uint16_t epochHits;
    int8_t rssi;
    bool alerted;
};

struct SentinelState {
    portMUX_TYPE mux;

    // Wi-Fi promiscuous counters (written under mux inside RX callback, zero heap allocs)
    volatile uint32_t winDeauth;
    volatile uint32_t winDisassoc;
    volatile uint32_t winProbe;
    volatile uint32_t winBeacon;
    uint8_t lastDeauthSrc[6];
    bool hasDeauthSrc;
    SentinelAp aps[SENTINEL_MAX_APS];

    // BLE persistence table (written under mux inside NimBLE callback)
    SentinelBleEntry ble[SENTINEL_MAX_BLE];
    volatile uint32_t sweepBleSpamCnt;
    uint32_t bleEpoch;

    // Sub-GHz CC1101 baseline RSSI across 4 ISM bands
    float rfBaseline[4];
    uint8_t rfSpikeStreak[4];
    bool rfBaselineInit;

    // Cumulative alert totals
    uint32_t totalDeauthAlerts;
    uint32_t totalEvilTwinAlerts;
    uint32_t totalTrackerAlerts;
    uint32_t totalSkimmerAlerts;
    uint32_t totalRfJamAlerts;

    // Bounded ring buffer of recent incidents
    SentinelEvent events[SENTINEL_MAX_EVENTS];
    size_t eventHead;
    size_t eventCount;

    bool logToSd;
};

static SentinelState *s_sentinel = nullptr;
static bool s_sentinelLogEnabled = true;

static void formatMacShort(const uint8_t mac[6], char out[18]) {
    snprintf(
        out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
}

static File openSentinelLog(String &outPath) {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) return File();
    if (!fs->exists("/NezhaLogs")) fs->mkdir("/NezhaLogs");
    for (uint16_t i = 0; i < 1000; ++i) {
        char p[44];
        snprintf(p, sizeof(p), "/NezhaLogs/sentinel_%03u.log", (unsigned)i);
        if (!fs->exists(p)) {
            outPath = p;
            return fs->open(p, FILE_WRITE);
        }
    }
    return File();
}

static void pushEvent(SentinelState *st, File &logFile, ThreatSev sev, const char *tag, const char *detail) {
    if (!st) return;
    SentinelEvent &ev = st->events[st->eventHead];
    ev.tsSec = millis() / 1000;
    ev.sev = sev;
    strncpy(ev.tag, tag, sizeof(ev.tag) - 1);
    ev.tag[sizeof(ev.tag) - 1] = '\0';
    strncpy(ev.detail, detail, sizeof(ev.detail) - 1);
    ev.detail[sizeof(ev.detail) - 1] = '\0';

    st->eventHead = (st->eventHead + 1) % SENTINEL_MAX_EVENTS;
    if (st->eventCount < SENTINEL_MAX_EVENTS) st->eventCount++;

    if (logFile) {
        const char *sevStr = (sev == ThreatSev::ALERT) ? "ALERT" : (sev == ThreatSev::WATCH ? "WATCH" : "INFO");
        logFile.printf("[%lus] %s %s: %s", (unsigned long)ev.tsSec, sevStr, ev.tag, ev.detail);
        if (bruceLocation.hasLocation()) {
            logFile.printf(" (%.5f,%.5f)", bruceLocation.lat(), bruceLocation.lon());
        }
        logFile.println();
        logFile.flush();
    }
}

/*********************************************************************
**  Engine 1: Wi-Fi Promiscuous Mgmt Watcher (zero-alloc RX callback)
**********************************************************************/
static void sentinelWifiRxCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    SentinelState *st = s_sentinel;
    if (!st || !buf || type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (pkt->rx_ctrl.sig_len < 24) return;
    const uint8_t *p = pkt->payload;
    uint8_t fc0 = p[0];
    uint8_t subtype = fc0 & 0xF0;

    if (subtype == 0xC0) { // Deauthentication
        portENTER_CRITICAL(&st->mux);
        st->winDeauth++;
        memcpy(st->lastDeauthSrc, p + 10, 6);
        st->hasDeauthSrc = true;
        portEXIT_CRITICAL(&st->mux);
    } else if (subtype == 0xA0) { // Disassociation
        portENTER_CRITICAL(&st->mux);
        st->winDisassoc++;
        memcpy(st->lastDeauthSrc, p + 10, 6);
        st->hasDeauthSrc = true;
        portEXIT_CRITICAL(&st->mux);
    } else if (subtype == 0x40) { // Probe Request
        portENTER_CRITICAL(&st->mux);
        st->winProbe++;
        portEXIT_CRITICAL(&st->mux);
    } else if (subtype == 0x80 && pkt->rx_ctrl.sig_len >= 38) { // Beacon
        // Fixed params at offset 24..35; CapInfo at 34..35 (bit 4 = Privacy)
        bool encrypted = (p[34] & 0x10) != 0;
        // Tagged parameters begin at offset 36; Tag 0 = SSID
        if (p[36] == 0x00) {
            uint8_t ssidLen = p[37];
            if (ssidLen > 0 && ssidLen <= 32 && (size_t)(38 + ssidLen) <= pkt->rx_ctrl.sig_len) {
                char ssid[24];
                size_t cp = (ssidLen < sizeof(ssid) - 1) ? ssidLen : (sizeof(ssid) - 1);
                memcpy(ssid, p + 38, cp);
                ssid[cp] = '\0';
                const uint8_t *bssid = p + 16;

                portENTER_CRITICAL(&st->mux);
                st->winBeacon++;
                int match = -1, empty = -1;
                for (size_t i = 0; i < SENTINEL_MAX_APS; ++i) {
                    if (st->aps[i].used) {
                        if (memcmp(st->aps[i].bssid, bssid, 6) == 0) {
                            match = (int)i;
                            break;
                        }
                    } else if (empty < 0) {
                        empty = (int)i;
                    }
                }
                if (match < 0 && empty >= 0) {
                    SentinelAp &ap = st->aps[empty];
                    ap.used = true;
                    memcpy(ap.bssid, bssid, 6);
                    memcpy(ap.ssid, ssid, sizeof(ap.ssid));
                    ap.encrypted = encrypted;
                    ap.twinAlerted = false;
                }
                portEXIT_CRITICAL(&st->mux);
            }
        }
    }
}

static bool sentinelWifiStart() {
    if (!radioHasMemForWifi(false)) return false;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous_rx_cb(sentinelWifiRxCb);
    wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    return true;
}

static void sentinelWifiStop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

static void sentinelEvaluateWifi(SentinelState *st, File &logFile) {
    uint32_t deauth = 0, disassoc = 0, probes = 0;
    uint8_t srcMac[6] = {};
    bool hasSrc = false;

    portENTER_CRITICAL(&st->mux);
    deauth = st->winDeauth;
    disassoc = st->winDisassoc;
    probes = st->winProbe;
    hasSrc = st->hasDeauthSrc;
    memcpy(srcMac, st->lastDeauthSrc, 6);
    st->winDeauth = 0;
    st->winDisassoc = 0;
    st->winProbe = 0;
    st->hasDeauthSrc = false;
    portEXIT_CRITICAL(&st->mux);

    if (deauth + disassoc >= 10) {
        st->totalDeauthAlerts++;
        char macStr[18] = "unknown";
        if (hasSrc) formatMacShort(srcMac, macStr);
        char msg[32];
        snprintf(msg, sizeof(msg), "%lu fr %.17s", (unsigned long)(deauth + disassoc), macStr);
        pushEvent(st, logFile, ThreatSev::ALERT, "DEAUTH", msg);
    }

    if (probes >= 40) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Probe burst %lu/2s", (unsigned long)probes);
        pushEvent(st, logFile, ThreatSev::WATCH, "PROBE", msg);
    }

    // Check for Evil-Twin: same SSID with conflicting encryption across distinct BSSIDs
    portENTER_CRITICAL(&st->mux);
    for (size_t i = 0; i < SENTINEL_MAX_APS; ++i) {
        if (!st->aps[i].used || st->aps[i].twinAlerted) continue;
        for (size_t j = i + 1; j < SENTINEL_MAX_APS; ++j) {
            if (!st->aps[j].used) continue;
            if (strcmp(st->aps[i].ssid, st->aps[j].ssid) == 0 &&
                st->aps[i].encrypted != st->aps[j].encrypted) {
                st->aps[i].twinAlerted = true;
                st->aps[j].twinAlerted = true;
                char ssidCopy[24];
                memcpy(ssidCopy, st->aps[i].ssid, sizeof(ssidCopy));
                portEXIT_CRITICAL(&st->mux);

                st->totalEvilTwinAlerts++;
                char msg[32];
                snprintf(msg, sizeof(msg), "Open+Enc %.20s", ssidCopy);
                pushEvent(st, logFile, ThreatSev::ALERT, "EVIL-TWIN", msg);

                portENTER_CRITICAL(&st->mux);
                break;
            }
        }
    }
    portEXIT_CRITICAL(&st->mux);
}

/*********************************************************************
**  Engine 2: BLE Stalker-Tracker & Skimmer Watcher (NimBLE 2.x)
**********************************************************************/
#if defined(SENTINEL_HAS_BLE)
class SentinelBleCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        SentinelState *st = s_sentinel;
        if (!st || !dev) return;

        const NimBLEAddress addr = dev->getAddress();
        const uint8_t *raw = addr.getVal();
        uint8_t mac[6];
        for (int i = 0; i < 6; ++i) mac[i] = raw[5 - i];

        uint8_t kind = 0;
        const char *label = nullptr;

        // 1) Classic Bluetooth skimmer / rogue serial adapter name signatures
        std::string nm = dev->getName();
        if (!nm.empty()) {
            if (nm.rfind("HC-05", 0) == 0 || nm.rfind("HC-06", 0) == 0 || nm.rfind("RNBT-", 0) == 0 ||
                nm.rfind("BT-SPP", 0) == 0) {
                kind = 2;
                label = "Skimmer?";
            }
        }

        // 2) Apple FindMy / AirTag & BLE Continuity spam detection
        if (kind == 0 && dev->haveManufacturerData()) {
            std::string mfg = dev->getManufacturerData();
            if (mfg.size() >= 3) {
                uint16_t cid = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
                uint8_t appleType = (uint8_t)mfg[2];
                if (cid == 0x004C) {
                    if (appleType == 0x12) {
                        kind = 1;
                        label = "AirTag";
                    } else if (appleType == 0x07 || appleType == 0x0F) {
                        portENTER_CRITICAL(&st->mux);
                        st->sweepBleSpamCnt++;
                        portEXIT_CRITICAL(&st->mux);
                    }
                }
            }
        }

        // 3) Samsung SmartTag (0xFD5A) & Tile (0xFEED) service / service-data UUIDs
        if (kind == 0) {
            auto checkUuid16 = [&](const NimBLEUUID &u) {
                if (kind != 0 || u.bitSize() != 16) return;
                std::string s = u.toString();
                unsigned int u16 = 0;
                if (sscanf(s.c_str(), "0x%x", &u16) != 1 && sscanf(s.c_str(), "%x", &u16) != 1) return;
                if (u16 == 0xFD5A) {
                    kind = 1;
                    label = "SmartTag";
                } else if (u16 == 0xFEED) {
                    kind = 1;
                    label = "Tile";
                }
            };
            uint8_t sc = dev->getServiceUUIDCount();
            for (uint8_t i = 0; i < sc && i < 4; ++i) checkUuid16(dev->getServiceUUID(i));
            uint8_t sdc = dev->getServiceDataCount();
            for (uint8_t i = 0; i < sdc && i < 4; ++i) checkUuid16(dev->getServiceDataUUID(i));
        }

        if (kind == 0 || !label) return;

        uint32_t now = millis();
        int8_t rssi = (int8_t)dev->getRSSI();

        portENTER_CRITICAL(&st->mux);
        int match = -1, empty = -1, oldest = 0;
        uint32_t oldestMs = UINT32_MAX;
        for (size_t i = 0; i < SENTINEL_MAX_BLE; ++i) {
            if (st->ble[i].used) {
                if (memcmp(st->ble[i].mac, mac, 6) == 0) {
                    match = (int)i;
                    break;
                }
                if (st->ble[i].lastMs < oldestMs) {
                    oldestMs = st->ble[i].lastMs;
                    oldest = (int)i;
                }
            } else if (empty < 0) {
                empty = (int)i;
            }
        }
        int idx = (match >= 0) ? match : ((empty >= 0) ? empty : oldest);
        SentinelBleEntry &e = st->ble[idx];
        if (match >= 0) {
            e.lastMs = now;
            e.rssi = rssi;
            if (e.lastEpoch != st->bleEpoch) {
                e.lastEpoch = st->bleEpoch;
                e.epochHits++;
            }
        } else {
            e.used = true;
            memcpy(e.mac, mac, 6);
            e.kind = kind;
            strncpy(e.label, label, sizeof(e.label) - 1);
            e.label[sizeof(e.label) - 1] = '\0';
            e.firstMs = now;
            e.lastMs = now;
            e.lastEpoch = st->bleEpoch;
            e.epochHits = 1;
            e.rssi = rssi;
            e.alerted = false;
        }
        portEXIT_CRITICAL(&st->mux);
    }
};

static SentinelBleCb s_bleCb;

static void sentinelRunBleSweep(SentinelState *st, File &logFile, uint32_t durationMs) {
    if (!radioHasMemForBle(false)) return;
    if (!radioSafeBleInit("", false)) return;

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) return;

    st->bleEpoch++;
    portENTER_CRITICAL(&st->mux);
    st->sweepBleSpamCnt = 0;
    portEXIT_CRITICAL(&st->mux);

    scan->setScanCallbacks(&s_bleCb, true);
    scan->setActiveScan(true);
    scan->setInterval(80);
    scan->setWindow(65);
    scan->start(durationMs, false, true);

    uint32_t t0 = millis();
    while (millis() - t0 < durationMs && !check(EscPress)) { vTaskDelay(pdMS_TO_TICKS(40)); }
    scan->stop();
    scan->clearResults();
    scan->setScanCallbacks(nullptr);

    // Evaluate BLE findings
    if (st->sweepBleSpamCnt >= 45) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Spam %lu pkts", (unsigned long)st->sweepBleSpamCnt);
        pushEvent(st, logFile, ThreatSev::ALERT, "BLE-SPAM", msg);
    }

    uint32_t now = millis();
    for (size_t i = 0; i < SENTINEL_MAX_BLE; ++i) {
        SentinelBleEntry &e = st->ble[i];
        if (!e.used || e.alerted) continue;

        char macShort[10];
        snprintf(macShort, sizeof(macShort), "%02X:%02X:%02X", e.mac[3], e.mac[4], e.mac[5]);

        if (e.kind == 2) { // Skimmer signature -> alert immediately
            e.alerted = true;
            st->totalSkimmerAlerts++;
            char msg[32];
            snprintf(msg, sizeof(msg), "%s %s %ddBm", e.label, macShort, (int)e.rssi);
            pushEvent(st, logFile, ThreatSev::ALERT, "SKIMMER", msg);
        } else if (e.kind == 1 && e.epochHits >= TRACKER_FOLLOW_EPOCHS &&
                   (now - e.firstMs) >= TRACKER_FOLLOW_MS) {
            e.alerted = true;
            st->totalTrackerAlerts++;
            char msg[32];
            snprintf(msg, sizeof(msg), "%s %s (%u)", e.label, macShort, (unsigned)e.epochHits);
            pushEvent(st, logFile, ThreatSev::ALERT, "STALKER", msg);
        }
    }
}
#endif

/*********************************************************************
**  Engine 3: CC1101 Sub-GHz Noise-Floor / Jamming Watcher
**********************************************************************/
static void sentinelRunRfSweep(SentinelState *st, File &logFile) {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) return;
    static const float kBands[4] = {315.0f, 433.92f, 868.35f, 915.0f};

    if (!initRfModule("rx", kBands[0])) return;
    for (int i = 0; i < 4 && !check(EscPress); ++i) {
        setMHZ(kBands[i]);
        vTaskDelay(pdMS_TO_TICKS(8));
        int rssi = ELECHOUSE_cc1101.getRssi();

        if (!st->rfBaselineInit) {
            st->rfBaseline[i] = (float)rssi;
        } else {
            if ((float)rssi >= st->rfBaseline[i] + 18.0f) {
                st->rfSpikeStreak[i]++;
                if (st->rfSpikeStreak[i] == 3) {
                    st->totalRfJamAlerts++;
                    char msg[32];
                    snprintf(msg, sizeof(msg), "%.1fMHz %ddBm", kBands[i], rssi);
                    pushEvent(st, logFile, ThreatSev::ALERT, "RF-JAM", msg);
                }
            } else {
                st->rfSpikeStreak[i] = 0;
                // Slow exponential moving average tracks normal ambient floor
                st->rfBaseline[i] = st->rfBaseline[i] * 0.85f + (float)rssi * 0.15f;
            }
        }
    }
    st->rfBaselineInit = true;
    deinitRfModule();
}

/*********************************************************************
**  Defensive HUD Renderer
**********************************************************************/
static void drawSentinelHud(const SentinelState *st, const char *activeBand, size_t scrollOff, bool logging) {
    const uint32_t totalAlerts = st->totalDeauthAlerts + st->totalEvilTwinAlerts + st->totalTrackerAlerts +
                                 st->totalSkimmerAlerts + st->totalRfJamAlerts;
    uint16_t bannerCol = (totalAlerts > 0) ? TFT_RED : (st->eventCount > 0 ? TFT_YELLOW : TFT_GREEN);
    const char *statusTxt = (totalAlerts > 0) ? "ALERT" : (st->eventCount > 0 ? "WATCH" : "CLEAR");

    const int innerX = BORDER_PAD_X;
    const int innerW = tftWidth - 2 * BORDER_PAD_X;
    int y = BORDER_PAD_Y + LH + 2;

    // Status pill bar
    tft.fillRect(innerX, y, innerW, LH + 4, uiSurfaceColor());
    tft.fillRect(innerX, y, 4, LH + 4, bannerCol);
    tft.setTextSize(FP);
    tft.setTextColor(bannerCol, uiSurfaceColor());
    tft.drawString(statusTxt, innerX + 8, y + 2);
    tft.setTextColor(uiTextBrightColor(), uiSurfaceColor());
    char rightHdr[32];
    snprintf(rightHdr, sizeof(rightHdr), "%s%s", activeBand, logging ? " [LOG]" : "");
    tft.drawRightString(rightHdr, innerX + innerW - 4, y + 2, 1);
    y += LH + 7;

    // Counters line
    tft.fillRect(innerX, y, innerW, LH + 2, bruceConfig.bgColor);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    char cntLine[64];
    snprintf(
        cntLine,
        sizeof(cntLine),
        "D:%lu Twn:%lu Trk:%lu Skm:%lu RF:%lu",
        (unsigned long)st->totalDeauthAlerts,
        (unsigned long)st->totalEvilTwinAlerts,
        (unsigned long)st->totalTrackerAlerts,
        (unsigned long)st->totalSkimmerAlerts,
        (unsigned long)st->totalRfJamAlerts
    );
    tft.drawString(cntLine, innerX, y);
    y += LH + 5;

    // Event feed area
    const int feedH = tftHeight - BORDER_PAD_X - LH - 4 - y;
    tft.fillRect(innerX, y, innerW, max(0, feedH), bruceConfig.bgColor);
    const int rowH = LH + 3;
    const int maxRows = max(1, feedH / rowH);

    if (st->eventCount == 0) {
        tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
        tft.drawString("Monitoring RF airspace...", innerX, y + 4);
    } else {
        for (int r = 0; r < maxRows && (scrollOff + (size_t)r) < st->eventCount; ++r) {
            // Newest first
            size_t revIdx = (st->eventHead + SENTINEL_MAX_EVENTS - 1 - scrollOff - (size_t)r) %
                            SENTINEL_MAX_EVENTS;
            const SentinelEvent &ev = st->events[revIdx];
            uint16_t col =
                (ev.sev == ThreatSev::ALERT) ? TFT_RED : (ev.sev == ThreatSev::WATCH ? TFT_YELLOW : uiTextBrightColor());
            tft.setTextColor(col, bruceConfig.bgColor);
            char row[48];
            snprintf(row, sizeof(row), "%s %s", ev.tag, ev.detail);
            tft.drawString(row, innerX, y + r * rowH);
        }
    }

    // Footer hint
    const int footY = tftHeight - BORDER_PAD_X - LH;
    tft.fillRect(innerX, footY, innerW, LH, bruceConfig.bgColor);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    tft.drawString("UP/DN Scroll  ESC Exit", innerX, footY);
}

void wireless_sentinel_run(SentinelMode mode) {
    SentinelState *st = (SentinelState *)calloc(1, sizeof(SentinelState));
    if (!st) {
        displayError("Not Enough RAM", true);
        return;
    }
    st->mux = portMUX_INITIALIZER_UNLOCKED;
    st->logToSd = s_sentinelLogEnabled;
    s_sentinel = st;

    File logFile;
    String logPath;
    if (st->logToSd) logFile = openSentinelLog(logPath);

    drawMainBorderWithTitle("Wireless Sentinel");
    size_t scrollOff = 0;
    uint8_t wifiCh = 1;
    uint32_t lastDrawMs = 0;
    bool wifiUp = false;

    pushEvent(st, logFile, ThreatSev::INFO, "START", "Sentinel watch armed");

    if (mode == SentinelMode::WIFI_SHIELD) {
        wifiUp = sentinelWifiStart();
        if (!wifiUp) pushEvent(st, logFile, ThreatSev::WATCH, "WIFI", "Low DMA for Wi-Fi");
    }

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();

        if (check(PrevPress) || check(UpPress)) {
            if (scrollOff > 0) scrollOff--;
        } else if (check(NextPress) || check(DownPress)) {
            if (scrollOff + 1 < st->eventCount) scrollOff++;
        }

        if (mode == SentinelMode::WIFI_SHIELD) {
            if (wifiUp) {
                esp_wifi_set_channel(wifiCh, WIFI_SECOND_CHAN_NONE);
                wifiCh = (wifiCh % 13) + 1;
                uint32_t t0 = millis();
                while (millis() - t0 < 2000 && !check(EscPress)) {
                    if (millis() - lastDrawMs >= 250) {
                        lastDrawMs = millis();
                        drawSentinelHud(st, "WIFI", scrollOff, (bool)logFile);
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                sentinelEvaluateWifi(st, logFile);
            } else {
                drawSentinelHud(st, "NO-DMA", scrollOff, (bool)logFile);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        } else if (mode == SentinelMode::BLE_SHIELD) {
            drawSentinelHud(st, "BLE", scrollOff, (bool)logFile);
#if defined(SENTINEL_HAS_BLE)
            sentinelRunBleSweep(st, logFile, 3000);
#else
            vTaskDelay(pdMS_TO_TICKS(500));
#endif
        } else if (mode == SentinelMode::RF_SHIELD) {
            drawSentinelHud(st, "SUBGHZ", scrollOff, (bool)logFile);
            sentinelRunRfSweep(st, logFile);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            // PATROL_ALL: cleanly time-slices Wi-Fi -> BLE -> CC1101 (4 MB safe)
            drawSentinelHud(st, "PATROL:WIFI", scrollOff, (bool)logFile);
            if (sentinelWifiStart()) {
                uint32_t t0 = millis();
                while (millis() - t0 < 2200 && !check(EscPress)) {
                    esp_wifi_set_channel(wifiCh, WIFI_SECOND_CHAN_NONE);
                    wifiCh = (wifiCh % 13) + 1;
                    vTaskDelay(pdMS_TO_TICKS(180));
                }
                sentinelEvaluateWifi(st, logFile);
                sentinelWifiStop();
            }
            if (check(EscPress)) break;

#if defined(SENTINEL_HAS_BLE)
            drawSentinelHud(st, "PATROL:BLE", scrollOff, (bool)logFile);
            sentinelRunBleSweep(st, logFile, 2500);
            if (check(EscPress)) break;
#endif
            if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
                drawSentinelHud(st, "PATROL:RF", scrollOff, (bool)logFile);
                sentinelRunRfSweep(st, logFile);
            }
        }

        drawSentinelHud(st, mode == SentinelMode::PATROL_ALL ? "PATROL" : "WATCH", scrollOff, (bool)logFile);
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (wifiUp) sentinelWifiStop();
    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    s_sentinel = nullptr;
    free(st);
    check(EscPress);
}

void wireless_sentinel_menu() {
    returnToMenu = false;
    while (!returnToMenu) {
        std::vector<Option> localOpts = {
            {"Multi-Sweep Patrol",    []() { wireless_sentinel_run(SentinelMode::PATROL_ALL); } },
            {"Wi-Fi Attack Shield",   []() { wireless_sentinel_run(SentinelMode::WIFI_SHIELD); }},
            {"BLE Stalker & Skimmer", []() { wireless_sentinel_run(SentinelMode::BLE_SHIELD); } },
            {"Sub-GHz Jamming Watch", []() { wireless_sentinel_run(SentinelMode::RF_SHIELD); }  },
            {String("Log Incidents: ") + (s_sentinelLogEnabled ? "ON" : "OFF"),
             []() { s_sentinelLogEnabled = !s_sentinelLogEnabled; }                             },
            {"Back",                  []() { returnToMenu = true; }                             },
        };
        loopOptions(localOpts, MENU_TYPE_SUBMENU, "Wireless Sentinel");
    }
    returnToMenu = false;
}
#endif
