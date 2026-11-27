#if !defined(LITE_VERSION)
#include "csi_radar.h"
#include "csi_radar_dsp.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>
#include <globals.h>
#include <sdkconfig.h>

#if defined(CONFIG_ESP_WIFI_CSI_ENABLED) && !defined(CONFIG_IDF_TARGET_ESP32C5)
#define NEZHA_NATIVE_CSI 1
#endif

namespace {

struct CsiRuntime {
    portMUX_TYPE mux;
    csi_radar::CsiMotionTracker tracker;
    uint8_t targetBssid[6];
    bool filterBssid;
    uint8_t channel;
    bool usingNativeCsi;
    volatile uint32_t rxSinceTick;
    volatile int8_t lastRssi;
};

static CsiRuntime *s_csi = nullptr;
static bool s_csiLogCsv = false;
static uint8_t s_csiChannel = 1;
static csi_radar::Sensitivity s_csiSens = csi_radar::Sensitivity::Medium;

#if defined(NEZHA_NATIVE_CSI)
static void csiRxCallback(void *ctx, wifi_csi_info_t *info) {
    CsiRuntime *rt = static_cast<CsiRuntime *>(ctx);
    if (!rt || !info || !info->buf || info->len < 4) return;
    if (rt->filterBssid && std::memcmp(info->mac, rt->targetBssid, 6) != 0) return;

    // Copy small raw IQ frame to stack (<= 128 B) before taking spinlock
    int copyLen = (info->len > 112) ? 112 : info->len;
    int8_t localIq[112];
    std::memcpy(localIq, info->buf, static_cast<size_t>(copyLen));
    int8_t rssi = info->rx_ctrl.rssi;

    portENTER_CRITICAL(&rt->mux);
    rt->tracker.feedCsi(localIq, copyLen);
    rt->lastRssi = rssi;
    rt->rxSinceTick++;
    portEXIT_CRITICAL(&rt->mux);
}
#endif

static void csiPromiscFallbackCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    CsiRuntime *rt = s_csi;
    if (!rt || !buf || rt->usingNativeCsi) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t *pkt = static_cast<const wifi_promiscuous_pkt_t *>(buf);
    if (pkt->rx_ctrl.sig_len < 24) return;
    const uint8_t *hdr = pkt->payload;

    if (rt->filterBssid) {
        // Check addr2 (TA) or addr3 (BSSID)
        if (std::memcmp(hdr + 10, rt->targetBssid, 6) != 0 &&
            std::memcmp(hdr + 16, rt->targetBssid, 6) != 0) {
            return;
        }
    }

    int8_t rssi = pkt->rx_ctrl.rssi;
    portENTER_CRITICAL(&rt->mux);
    rt->tracker.feedRssiSample(rssi);
    rt->lastRssi = rssi;
    rt->rxSinceTick++;
    portEXIT_CRITICAL(&rt->mux);
}

static const char *stateLabel(csi_radar::MotionState st) {
    switch (st) {
        case csi_radar::MotionState::CALIBRATING: return "CALIB";
        case csi_radar::MotionState::STILL: return "STILL";
        case csi_radar::MotionState::PRESENCE: return "PRESENCE";
        case csi_radar::MotionState::MOTION: return "MOTION!";
    }
    return "STILL";
}

static uint16_t stateColor(csi_radar::MotionState st) {
    switch (st) {
        case csi_radar::MotionState::CALIBRATING: return TFT_CYAN;
        case csi_radar::MotionState::STILL: return TFT_GREEN;
        case csi_radar::MotionState::PRESENCE: return TFT_YELLOW;
        case csi_radar::MotionState::MOTION: return TFT_RED;
    }
    return TFT_GREEN;
}

static const char *sensLabel(csi_radar::Sensitivity s) {
    switch (s) {
        case csi_radar::Sensitivity::Low: return "LOW";
        case csi_radar::Sensitivity::High: return "HIGH";
        case csi_radar::Sensitivity::Medium:
        default: return "MED";
    }
}

static File openCsiLogFile(String &outPath) {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) return File();
    if (!fs->exists("/NezhaLogs")) fs->mkdir("/NezhaLogs");
    for (uint16_t i = 0; i < 1000; ++i) {
        char p[40];
        snprintf(p, sizeof(p), "/NezhaLogs/csi_%03u.csv", (unsigned)i);
        if (!fs->exists(p)) {
            outPath = p;
            File f = fs->open(p, FILE_WRITE);
            if (f) f.println("ts_ms,score,state,rssi,pkt_rate");
            return f;
        }
    }
    return File();
}

static void drawCsiRadarScreen(
    const csi_radar::CsiMotionTracker &snap, bool nativeCsi, uint8_t ch, int8_t rssi, uint32_t pps,
    const char *apLabel, bool logging
) {
    const int x = BORDER_PAD_X;
    const int w = tftWidth - 2 * BORDER_PAD_X;
    int y = BORDER_PAD_Y + LH + 2;

    // 1) Status Header Bar
    uint16_t stCol = stateColor(snap.state);
    tft.fillRect(x, y, w, LH + 4, uiSurfaceColor());
    tft.fillRect(x, y, 4, LH + 4, stCol);
    tft.setTextSize(FP);
    tft.setTextColor(stCol, uiSurfaceColor());
    tft.drawString(stateLabel(snap.state), x + 8, y + 2);

    char rightInfo[40];
    snprintf(
        rightInfo,
        sizeof(rightInfo),
        "%s CH%u %u%% %s",
        nativeCsi ? "CSI" : "RSSI",
        (unsigned)ch,
        (unsigned)snap.motionScore,
        sensLabel(snap.sensitivity)
    );
    tft.setTextColor(uiTextBrightColor(), uiSurfaceColor());
    tft.drawRightString(rightInfo, x + w - 4, y + 2, 1);
    y += LH + 6;

    // 2) Telemetry Sub-bar
    tft.fillRect(x, y, w, LH + 2, bruceConfig.bgColor);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    char meta[64];
    snprintf(
        meta,
        sizeof(meta),
        "%.12s %ddBm %lu/s%s",
        apLabel,
        (int)rssi,
        (unsigned long)pps,
        logging ? " LOG" : ""
    );
    tft.drawString(meta, x, y);
    y += LH + 4;

    // 3) Scrolling Motion Waveform Graph
    const int footY = tftHeight - BORDER_PAD_X - LH;
    const int specH = (nativeCsi && snap.activeBins > 0) ? 20 : 0;
    const int graphH = max(24, footY - y - specH - (specH ? 6 : 3));

    tft.fillRect(x, y, w, graphH, uiSurfaceColor());
    tft.drawRect(x, y, w, graphH, uiBorderDimColor());

    // Reference threshold lines (PRESENCE ~30%, MOTION ~65%)
    const int presY = y + graphH - 2 - ((graphH - 4) * 30) / 100;
    const int motY = y + graphH - 2 - ((graphH - 4) * 65) / 100;
    for (int gx = x + 2; gx < x + w - 2; gx += 6) {
        tft.drawFastHLine(gx, presY, 3, TFT_DARKGREY);
        tft.drawFastHLine(gx, motY, 3, TFT_MAROON);
    }

    // Plot oldest -> newest across available width
    const int innerW = w - 4;
    int prevX = -1, prevY = -1;
    for (size_t i = 0; i < csi_radar::CSI_WAVE_LEN; ++i) {
        size_t idx = (snap.waveHead + i) % csi_radar::CSI_WAVE_LEN;
        uint8_t val = snap.waveform[idx];
        int px = x + 2 + (int)((i * (size_t)innerW) / csi_radar::CSI_WAVE_LEN);
        int py = y + graphH - 2 - ((graphH - 4) * (int)val) / 100;
        uint16_t lineCol = (val >= 65) ? TFT_RED : ((val >= 30) ? TFT_YELLOW : bruceConfig.priColor);
        if (prevX >= 0) tft.drawLine(prevX, prevY, px, py, lineCol);
        prevX = px;
        prevY = py;
    }
    y += graphH + 3;

    // 4) Subcarrier Perturbation Bar Strip (native CSI mode)
    if (specH > 0) {
        tft.fillRect(x, y, w, specH, bruceConfig.bgColor);
        const int bins = snap.activeBins;
        const int barStep = max(1, w / bins);
        for (int i = 0; i < bins; ++i) {
            int bx = x + (i * w) / bins;
            float norm = snap.lastDelta[i] * 2.8f;
            if (norm > 1.0f) norm = 1.0f;
            int bh = max(1, (int)(norm * (float)(specH - 2)));
            uint16_t col = (norm > 0.65f) ? TFT_RED : ((norm > 0.35f) ? TFT_YELLOW : bruceConfig.secColor);
            tft.fillRect(bx, y + specH - bh, max(1, barStep - 1), bh, col);
        }
    }

    // 5) Footer Controls
    tft.fillRect(x, footY, w, LH, bruceConfig.bgColor);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    tft.drawString("OK:Calib  <>:Sens  ESC:Exit", x, footY);
}

static void runCsiRadarSession(bool filterBssid, const uint8_t targetBssid[6], uint8_t ch, const char *apLabel) {
    if (!radioHasMemForWifi(false)) {
        displayError(radioLowRamError("Low DMA for CSI"), true);
        return;
    }

    CsiRuntime *rt = static_cast<CsiRuntime *>(calloc(1, sizeof(CsiRuntime)));
    if (!rt) {
        displayError("Not Enough RAM", true);
        return;
    }

    rt->mux = portMUX_INITIALIZER_UNLOCKED;
    rt->tracker.reset();
    rt->tracker.sensitivity = s_csiSens;
    rt->filterBssid = filterBssid;
    if (filterBssid && targetBssid) std::memcpy(rt->targetBssid, targetBssid, 6);
    rt->channel = (ch >= 1 && ch <= 13) ? ch : 1;
    rt->usingNativeCsi = false;
    rt->lastRssi = -90;
    s_csi = rt;

    File logFile;
    String logPath;
    if (s_csiLogCsv) logFile = openCsiLogFile(logPath);

    // Put Wi-Fi in STA + promiscuous RX on locked channel so beacons & data frames feed CSI
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(rt->channel, WIFI_SECOND_CHAN_NONE);

#if defined(NEZHA_NATIVE_CSI)
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true;
    cfg.htltf_en = true;
    cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true;
    cfg.channel_filter_en = true;
    cfg.manu_scale = false;
    cfg.shift = false;

    if (esp_wifi_set_csi_config(&cfg) == ESP_OK &&
        esp_wifi_set_csi_rx_cb(csiRxCallback, rt) == ESP_OK &&
        esp_wifi_set_csi(true) == ESP_OK) {
        rt->usingNativeCsi = true;
    }
#endif

    if (!rt->usingNativeCsi) {
        wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous_rx_cb(csiPromiscFallbackCb);
    }

    drawMainBorderWithTitle("Wi-Fi CSI Radar");
    check(SelPress);
    check(EscPress);

    uint32_t lastDrawMs = 0;
    uint32_t lastPpsMs = millis();
    uint32_t pps = 0;
    csi_radar::CsiMotionTracker snapshot;

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();

        if (check(SelPress)) {
            portENTER_CRITICAL(&rt->mux);
            rt->tracker.reset();
            rt->tracker.sensitivity = s_csiSens;
            portEXIT_CRITICAL(&rt->mux);
        }

        if (check(NextPress) || check(UpPress)) {
            s_csiSens = static_cast<csi_radar::Sensitivity>(((uint8_t)s_csiSens + 1) % 3);
            portENTER_CRITICAL(&rt->mux);
            rt->tracker.sensitivity = s_csiSens;
            portEXIT_CRITICAL(&rt->mux);
        } else if (check(PrevPress) || check(DownPress)) {
            s_csiSens = static_cast<csi_radar::Sensitivity>(((uint8_t)s_csiSens + 2) % 3);
            portENTER_CRITICAL(&rt->mux);
            rt->tracker.sensitivity = s_csiSens;
            portEXIT_CRITICAL(&rt->mux);
        }

        uint32_t now = millis();
        if (now - lastPpsMs >= 1000) {
            portENTER_CRITICAL(&rt->mux);
            pps = rt->rxSinceTick;
            rt->rxSinceTick = 0;
            portEXIT_CRITICAL(&rt->mux);
            lastPpsMs = now;
        }

        if (now - lastDrawMs >= 140) {
            lastDrawMs = now;
            int8_t rssiSnap = -90;
            portENTER_CRITICAL(&rt->mux);
            snapshot = rt->tracker;
            rssiSnap = rt->lastRssi;
            portEXIT_CRITICAL(&rt->mux);

            drawCsiRadarScreen(snapshot, rt->usingNativeCsi, rt->channel, rssiSnap, pps, apLabel, (bool)logFile);

            if (logFile) {
                logFile.printf(
                    "%lu,%u,%s,%d,%lu\n",
                    (unsigned long)now,
                    (unsigned)snapshot.motionScore,
                    stateLabel(snapshot.state),
                    (int)rssiSnap,
                    (unsigned long)pps
                );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }

#if defined(NEZHA_NATIVE_CSI)
    if (rt->usingNativeCsi) {
        esp_wifi_set_csi(false);
        esp_wifi_set_csi_rx_cb(nullptr, nullptr);
    }
#endif
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    s_csi = nullptr;
    free(rt);
    check(EscPress);
}

static void csiPickApAndStart() {
    if (!radioHasMemForWifi(false)) {
        displayError(radioLowRamError("Low DMA for WiFi"), true);
        return;
    }
    displayRedStripe("Scanning APs for CSI...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks(false, false);
    if (n <= 0) {
        WiFi.scanDelete();
        displayWarning("No APs found", true);
        return;
    }

    int picked = -1;
    struct ApChoice {
        uint8_t bssid[6];
        uint8_t ch;
        String label;
    };
    std::vector<ApChoice> aps;
    int maxShow = (n > 16) ? 16 : n;
    aps.reserve(maxShow);

    std::vector<Option> localOpts;
    localOpts.reserve(maxShow + 1);
    for (int i = 0; i < maxShow; ++i) {
        ApChoice item{};
        const uint8_t *b = WiFi.BSSID(i);
        if (b) std::memcpy(item.bssid, b, 6);
        item.ch = (uint8_t)WiFi.channel(i);
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) ssid = "<Hidden>";
        item.label = ssid;
        String menuText = ssid.substring(0, 14) + " CH" + String(item.ch) + " " + String(WiFi.RSSI(i));
        aps.push_back(item);
        int idx = (int)aps.size() - 1;
        localOpts.push_back({menuText, [idx, &picked]() { picked = idx; }});
    }
    WiFi.scanDelete();
    localOpts.push_back({"Cancel", []() {}});

    loopOptions(localOpts, MENU_TYPE_SUBMENU, "Lock CSI Target AP");
    if (picked >= 0 && picked < (int)aps.size()) {
        runCsiRadarSession(true, aps[picked].bssid, aps[picked].ch, aps[picked].label.c_str());
    }
}

} // namespace

void csi_radar_menu() {
    returnToMenu = false;
    while (!returnToMenu) {
        std::vector<Option> localOpts = {
            {"Lock Target AP (Best)", csiPickApAndStart},
            {"Channel Watch (CH " + String((unsigned)s_csiChannel) + ")",
             []() {
                 String lbl = "CH" + String((unsigned)s_csiChannel) + " ALL";
                 runCsiRadarSession(false, nullptr, s_csiChannel, lbl.c_str());
             }},
            {"Set Channel: " + String((unsigned)s_csiChannel),
             []() { s_csiChannel = (s_csiChannel % 13) + 1; }},
            {String("Sensitivity: ") + sensLabel(s_csiSens),
             []() { s_csiSens = static_cast<csi_radar::Sensitivity>(((uint8_t)s_csiSens + 1) % 3); }},
            {String("Log CSV to SD: ") + (s_csiLogCsv ? "ON" : "OFF"),
             []() { s_csiLogCsv = !s_csiLogCsv; }},
            {"Back", []() { returnToMenu = true; }},
        };
        loopOptions(localOpts, MENU_TYPE_SUBMENU, "Wi-Fi CSI Radar");
    }
    returnToMenu = false;
}
#endif
