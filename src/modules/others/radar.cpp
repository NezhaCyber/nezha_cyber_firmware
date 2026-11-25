#if !defined(LITE_VERSION)
#include "radar.h"

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline bool isChinese() { return false; }
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
static inline int16_t utf8PixelWidth(const String &s, uint8_t ts) { return (int16_t)(s.length() * LW * ts); }
static inline String utf8TruncateToWidth(const String &s, int16_t maxPx, uint8_t ts) {
    int maxChars = max(1, (int)(maxPx / max(1, (int)(LW * ts))));
    return ((int)s.length() > maxChars) ? s.substring(0, maxChars) : s;
}
} // namespace bruce_i18n
#endif
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#if !defined(RADIO_MEM_TWO_TIER)
static inline size_t radioFreeDmaBytes() { return heap_caps_get_free_size(MALLOC_CAP_DMA); }
static inline void radioBleMinThresholds(size_t &minBlock, size_t &minFree) {
    minBlock = 18 * 1024;
    minFree = 32 * 1024;
}
static inline bool radioDmaAtLeast(size_t minBlock, size_t minFree) {
    return radioLargestDmaBlock() >= minBlock && radioFreeDmaBytes() >= minFree;
}
static inline bool radioSafeBleInit(const std::string &name, bool = true) {
    return NimBLEDevice::isInitialized() || NimBLEDevice::init(name);
}
#endif
#include "core/sd_functions.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include "modules/ble/ble_common.h"
#include <NimBLEDevice.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <esp_wifi.h>
#include <globals.h>
#include <vector>

namespace {

constexpr size_t RADAR_MAX_TARGETS = 48;
constexpr uint32_t RADAR_STALE_MS = 28000;
constexpr uint32_t RADAR_BLE_SCAN_MS = 2200;
constexpr float RADAR_PI = 3.14159265f;
constexpr float RADAR_MAX_DIST_M = 30.0f;
constexpr float RADAR_TRIANGULATE_STEP_M = 2.0f;

// Bright legible RGB565 colors on dark backgrounds
constexpr uint16_t COL_BLE_SAMSUNG = 0x4C5F; // Sky blue
constexpr uint16_t COL_BLE_MS = 0x5EBF;      // Light teal

enum class RadarMode : uint8_t { BOTH = 0, WIFI_ONLY = 1, BLE_ONLY = 2 };

enum class RadarTargetType : uint8_t { WIFI = 0, BLE = 1 };

enum class RadarCategory : uint8_t {
    WIFI_OPEN = 0,
    WIFI_SECURED = 1,
    WIFI_HIDDEN = 2,
    BLE_TRACKER = 3,
    BLE_APPLE = 4,
    BLE_SAMSUNG = 5,
    BLE_GOOGLE = 6,
    BLE_MICROSOFT = 7,
    BLE_AUDIO_HID = 8,
    BLE_GENERIC = 9,
};

struct RadarTarget {
    bool active = false;
    RadarTargetType type = RadarTargetType::WIFI;
    RadarCategory category = RadarCategory::BLE_GENERIC;
    uint8_t mac[6] = {0};
    char name[22] = {0};
    char subtype[14] = {0};
    int8_t rssi = -100;
    int8_t refRssi1m = -59; // Calibrated 1m RSSI reference for distance estimation
    uint8_t channel = 0;
    uint8_t authMode = 0;
    float angleRad = 0.0f;
    float distM = 10.0f;
    bool hasTriangulated = false;
    float relX = 0.0f; // Relative meters (+East)
    float relY = 0.0f; // Relative meters (+North)
    bool hasGps = false;
    double lat = 0.0;
    double lon = 0.0;
    uint32_t lastSeenMs = 0;
};

struct RadarContext {
    RadarTarget *targets = nullptr;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    bool hasLocked = false;
    uint8_t lockedMac[6] = {0};
    RadarTargetType lockedType = RadarTargetType::WIFI;
    bool showLabels = true;

    // Optional live GPS state (non-blocking)
    bool gpsStarted = false;
    bool gpsFix = false;
    double gpsLat = 0.0;
    double gpsLon = 0.0;
    uint32_t gpsSats = 0;
};

static RadarContext *s_radarCtx = nullptr;

static float macToAngleRad(const uint8_t mac[6]) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) {
        h ^= mac[i];
        h *= 16777619u;
    }
    return (float)(h % 3600u) * (2.0f * RADAR_PI / 3600.0f);
}

static String formatMac(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static float estimateDistanceM(int8_t rssi, int8_t ref1m) {
    // Log-distance path loss: d = 10 ^ ((RSSI_1m - RSSI) / (10 * n)), n = 2.5
    float exponent = (float)(ref1m - rssi) / 25.0f;
    float d = powf(10.0f, exponent);
    if (d < 0.2f) d = 0.2f;
    if (d > 60.0f) d = 60.0f;
    return d;
}

static String formatDistShort(float d) {
    char buf[10];
    if (d < 10.0f) snprintf(buf, sizeof(buf), "%.1fm", d);
    else snprintf(buf, sizeof(buf), "%.0fm", d);
    return String(buf);
}

static const char *bearingToCardinal(float deg) {
    static const char *dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)floorf((deg + 22.5f) / 45.0f) & 7;
    return dirs[idx];
}

static float vectorToBearingDeg(float dx, float dy) {
    // 0 deg = North (+Y), 90 deg = East (+X)
    float deg = atan2f(dx, dy) * (180.0f / RADAR_PI);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

static uint16_t categoryColor(RadarCategory cat) {
    switch (cat) {
        case RadarCategory::WIFI_OPEN: return TFT_GREEN;
        case RadarCategory::WIFI_SECURED: return TFT_CYAN;
        case RadarCategory::WIFI_HIDDEN: return TFT_YELLOW;
        case RadarCategory::BLE_TRACKER: return TFT_RED;
        case RadarCategory::BLE_APPLE: return TFT_WHITE;
        case RadarCategory::BLE_SAMSUNG: return COL_BLE_SAMSUNG;
        case RadarCategory::BLE_GOOGLE: return TFT_ORANGE;
        case RadarCategory::BLE_MICROSOFT: return COL_BLE_MS;
        case RadarCategory::BLE_AUDIO_HID: return TFT_YELLOW;
        default: return TFT_MAGENTA;
    }
}

static const char *categoryName(RadarCategory cat) {
    switch (cat) {
        case RadarCategory::WIFI_OPEN: return "Open WiFi";
        case RadarCategory::WIFI_SECURED: return "Secured WiFi";
        case RadarCategory::WIFI_HIDDEN: return "Hidden AP";
        case RadarCategory::BLE_TRACKER: return "Tracker/Beacon";
        case RadarCategory::BLE_APPLE: return "Apple BLE";
        case RadarCategory::BLE_SAMSUNG: return "Samsung BLE";
        case RadarCategory::BLE_GOOGLE: return "Google BLE";
        case RadarCategory::BLE_MICROSOFT: return "Microsoft BLE";
        case RadarCategory::BLE_AUDIO_HID: return "Audio / HID";
        default: return "BLE Device";
    }
}

static const char *wifiAuthShort(uint8_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
        default: return "SEC";
    }
}

static String targetDisplayName(const RadarTarget &t) {
    if (t.name[0] != '\0') return String(t.name);
    if (t.subtype[0] != '\0') return String(t.subtype);
    return formatMac(t.mac).substring(9);
}

static void drawCategoryGlyph(int x, int y, RadarCategory cat, uint16_t col) {
    switch (cat) {
        case RadarCategory::WIFI_OPEN:
        case RadarCategory::WIFI_SECURED:
            tft.fillRect(x - 2, y - 2, 5, 5, col);
            break;
        case RadarCategory::WIFI_HIDDEN:
            tft.fillTriangle(x, y - 3, x - 3, y + 2, x + 3, y + 2, col);
            break;
        case RadarCategory::BLE_TRACKER:
            // Highlighted diamond + outer alert halo for AirTag/SmartTag/Tile/iBeacon
            tft.drawCircle(x, y, 4, TFT_ORANGE);
            tft.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
            tft.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
            break;
        case RadarCategory::BLE_APPLE:
            // White diamond for Apple Continuity devices
            tft.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
            tft.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
            break;
        default:
            tft.fillCircle(x, y, 2, col);
            break;
    }
}

static void classifyBleDevice(
    const NimBLEAdvertisedDevice *dev, RadarCategory &cat, char *subtypeBuf, size_t subtypeSz, int8_t &ref1m
) {
    cat = RadarCategory::BLE_GENERIC;
    subtypeBuf[0] = '\0';
    ref1m = -59;

    // 1) Advertised TX Power (0m TX power -> approximate 1m RSSI by subtracting ~41 dB)
    if (dev->haveTXPower()) {
        int8_t tx = dev->getTXPower();
        if (tx >= -20 && tx <= 20) ref1m = (int8_t)constrain((int)tx - 41, -78, -35);
    }

    // 2) Manufacturer data (Company ID + Apple Continuity / iBeacon decoding)
    if (dev->haveManufacturerData()) {
        std::string mfg = dev->getManufacturerData();
        if (mfg.size() >= 2) {
            uint16_t cid = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
            if (cid == 0x004C) {
                cat = RadarCategory::BLE_APPLE;
                strncpy(subtypeBuf, "Apple", subtypeSz - 1);
                if (mfg.size() >= 3) {
                    uint8_t appleType = (uint8_t)mfg[2];
                    if (appleType == 0x02 && mfg.size() >= 25 && (uint8_t)mfg[3] == 0x15) {
                        cat = RadarCategory::BLE_TRACKER;
                        strncpy(subtypeBuf, "iBeacon", subtypeSz - 1);
                        int8_t cal = (int8_t)mfg[24];
                        if (cal >= -90 && cal <= -30) ref1m = cal;
                    } else if (appleType == 0x12) {
                        cat = RadarCategory::BLE_TRACKER;
                        strncpy(subtypeBuf, "AirTag", subtypeSz - 1);
                    } else if (appleType == 0x07) {
                        cat = RadarCategory::BLE_AUDIO_HID;
                        strncpy(subtypeBuf, "AirPods", subtypeSz - 1);
                    } else if (appleType == 0x10) {
                        strncpy(subtypeBuf, "iPhone/Mac", subtypeSz - 1);
                    } else if (appleType == 0x05) {
                        strncpy(subtypeBuf, "AirDrop", subtypeSz - 1);
                    } else if (appleType == 0x09) {
                        strncpy(subtypeBuf, "AirPlay", subtypeSz - 1);
                    } else if (appleType == 0x0C) {
                        strncpy(subtypeBuf, "Handoff", subtypeSz - 1);
                    }
                }
            } else if (cid == 0x0075) {
                cat = RadarCategory::BLE_SAMSUNG;
                strncpy(subtypeBuf, "Samsung", subtypeSz - 1);
            } else if (cid == 0x00E0) {
                cat = RadarCategory::BLE_GOOGLE;
                strncpy(subtypeBuf, "Google", subtypeSz - 1);
            } else if (cid == 0x0006) {
                cat = RadarCategory::BLE_MICROSOFT;
                strncpy(subtypeBuf, "Microsoft", subtypeSz - 1);
            } else if (cid == 0x038F) {
                strncpy(subtypeBuf, "Xiaomi", subtypeSz - 1);
            } else if (cid == 0x027D) {
                strncpy(subtypeBuf, "Huawei", subtypeSz - 1);
            } else if (cid == 0x0087) {
                cat = RadarCategory::BLE_AUDIO_HID;
                strncpy(subtypeBuf, "Garmin", subtypeSz - 1);
            } else if (cid == 0x0059) {
                strncpy(subtypeBuf, "Nordic", subtypeSz - 1);
            }
        }
    }

    // 3) Known BLE service UUIDs & service-data UUIDs (SmartTag, Tile, Eddystone, FastPair, HID)
    auto checkUuid = [&](const NimBLEUUID &u) {
        if (u.bitSize() != 16) return;
        std::string s = u.toString(); // e.g. "0xfd5a" or "fd5a"
        unsigned int val = 0;
        if (sscanf(s.c_str(), "0x%x", &val) != 1 && sscanf(s.c_str(), "%x", &val) != 1) return;
        switch (val) {
            case 0xFD5A:
                cat = RadarCategory::BLE_TRACKER;
                strncpy(subtypeBuf, "SmartTag", subtypeSz - 1);
                break;
            case 0xFEED:
                cat = RadarCategory::BLE_TRACKER;
                strncpy(subtypeBuf, "Tile", subtypeSz - 1);
                break;
            case 0xFEAA:
                cat = RadarCategory::BLE_TRACKER;
                strncpy(subtypeBuf, "Eddystone", subtypeSz - 1);
                break;
            case 0xFE2C:
                cat = RadarCategory::BLE_GOOGLE;
                strncpy(subtypeBuf, "FastPair", subtypeSz - 1);
                break;
            case 0x1812:
                cat = RadarCategory::BLE_AUDIO_HID;
                strncpy(subtypeBuf, "BLE HID", subtypeSz - 1);
                break;
            case 0x180D:
                cat = RadarCategory::BLE_AUDIO_HID;
                strncpy(subtypeBuf, "HeartRate", subtypeSz - 1);
                break;
            case 0x184E:
            case 0x184F:
            case 0x1853:
                cat = RadarCategory::BLE_AUDIO_HID;
                strncpy(subtypeBuf, "LE Audio", subtypeSz - 1);
                break;
            default: break;
        }
    };

    uint8_t svcCnt = dev->getServiceUUIDCount();
    for (uint8_t i = 0; i < svcCnt && i < 4; ++i) checkUuid(dev->getServiceUUID(i));
    uint8_t sdCnt = dev->getServiceDataCount();
    for (uint8_t i = 0; i < sdCnt && i < 4; ++i) checkUuid(dev->getServiceDataUUID(i));

    // 4) Appearance fallback if still generic
    if (cat == RadarCategory::BLE_GENERIC && dev->haveAppearance()) {
        uint16_t group = dev->getAppearance() >> 6;
        if (group == 15) {
            cat = RadarCategory::BLE_AUDIO_HID;
            if (!subtypeBuf[0]) strncpy(subtypeBuf, "HID", subtypeSz - 1);
        } else if (group == 3) {
            cat = RadarCategory::BLE_AUDIO_HID;
            if (!subtypeBuf[0]) strncpy(subtypeBuf, "Watch", subtypeSz - 1);
        } else if (group == 33 || group == 34) {
            cat = RadarCategory::BLE_AUDIO_HID;
            if (!subtypeBuf[0]) strncpy(subtypeBuf, "Audio", subtypeSz - 1);
        }
    }

    subtypeBuf[subtypeSz - 1] = '\0';
}

static void radarUpsertTarget(
    RadarContext *ctx, RadarTargetType type, RadarCategory cat, const uint8_t mac[6], const char *name,
    const char *subtype, int8_t rssi, int8_t ref1m, uint8_t channel, uint8_t authMode
) {
    if (!ctx || !ctx->targets) return;
    uint32_t now = millis();
    portENTER_CRITICAL(&ctx->mux);

    int matchIdx = -1;
    int freeIdx = -1;
    int oldestIdx = 0;
    uint32_t oldestMs = 0xFFFFFFFFu;

    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        RadarTarget &t = ctx->targets[i];
        if (t.active) {
            if (t.type == type && memcmp(t.mac, mac, 6) == 0) {
                matchIdx = (int)i;
                break;
            }
            if (t.lastSeenMs < oldestMs) {
                oldestMs = t.lastSeenMs;
                oldestIdx = (int)i;
            }
        } else if (freeIdx < 0) {
            freeIdx = (int)i;
        }
    }

    int idx = (matchIdx >= 0) ? matchIdx : ((freeIdx >= 0) ? freeIdx : oldestIdx);
    RadarTarget &slot = ctx->targets[idx];
    if (matchIdx >= 0) {
        slot.rssi = (int8_t)((slot.rssi * 3 + rssi * 5) / 8);
        if (cat != RadarCategory::BLE_GENERIC || slot.category == RadarCategory::BLE_GENERIC) {
            slot.category = cat;
        }
    } else {
        slot = RadarTarget{};
        slot.active = true;
        slot.type = type;
        slot.category = cat;
        memcpy(slot.mac, mac, 6);
        slot.rssi = rssi;
        slot.angleRad = macToAngleRad(mac);
    }

    slot.refRssi1m = ref1m;
    slot.distM = estimateDistanceM(slot.rssi, slot.refRssi1m);
    if (!slot.hasTriangulated) {
        slot.relX = slot.distM * cosf(slot.angleRad);
        slot.relY = -slot.distM * sinf(slot.angleRad); // Screen -Y = North (+Y)
    }

    if (name && name[0] != '\0') {
        strncpy(slot.name, name, sizeof(slot.name) - 1);
        slot.name[sizeof(slot.name) - 1] = '\0';
    }
    if (subtype && subtype[0] != '\0') {
        strncpy(slot.subtype, subtype, sizeof(slot.subtype) - 1);
        slot.subtype[sizeof(slot.subtype) - 1] = '\0';
    }
    if (channel > 0) slot.channel = channel;
    slot.authMode = authMode;

    if (ctx->gpsFix) {
        slot.hasGps = true;
        slot.lat = ctx->gpsLat;
        slot.lon = ctx->gpsLon;
    }
    slot.lastSeenMs = now;

    portEXIT_CRITICAL(&ctx->mux);
}

class RadarBleCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!dev || !s_radarCtx) return;
        const NimBLEAddress addr = dev->getAddress();
        const uint8_t *raw = addr.getVal();
        uint8_t mac[6];
        // NimBLE stores address bytes in little-endian order
        for (int i = 0; i < 6; ++i) mac[i] = raw[5 - i];

        RadarCategory cat = RadarCategory::BLE_GENERIC;
        char subtype[14] = {0};
        int8_t ref1m = -59;
        classifyBleDevice(dev, cat, subtype, sizeof(subtype), ref1m);

        std::string nm = dev->getName();
        radarUpsertTarget(
            s_radarCtx,
            RadarTargetType::BLE,
            cat,
            mac,
            nm.empty() ? "" : nm.c_str(),
            subtype,
            (int8_t)dev->getRSSI(),
            ref1m,
            0,
            0
        );
    }
};

static RadarBleCallbacks s_radarBleCb;

static int distToRadius(float distM, int maxR) {
    // Log-compressed distance scale so 0.5m..8m gets generous visual clarity
    float norm = log10f(1.0f + constrain(distM, 0.2f, RADAR_MAX_DIST_M)) / log10f(1.0f + RADAR_MAX_DIST_M);
    int r = (int)(norm * (float)(maxR - 6)) + 4;
    return constrain(r, 4, maxR - 3);
}

static const char *modeLabel(RadarMode mode) {
    switch (mode) {
        case RadarMode::WIFI_ONLY: return "Filter: WiFi";
        case RadarMode::BLE_ONLY: return "Filter: BLE";
        default: return "Filter: Both";
    }
}

static const char *modeShort(RadarMode mode) {
    switch (mode) {
        case RadarMode::WIFI_ONLY: return "WIFI";
        case RadarMode::BLE_ONLY: return "BLE";
        default: return "W+B";
    }
}

static void pruneStaleTargets(RadarContext &ctx) {
    uint32_t now = millis();
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        if (ctx.targets[i].active && (now - ctx.targets[i].lastSeenMs) > RADAR_STALE_MS) {
            bool isLocked = ctx.hasLocked && ctx.targets[i].type == ctx.lockedType &&
                            memcmp(ctx.targets[i].mac, ctx.lockedMac, 6) == 0;
            if (!isLocked || (now - ctx.targets[i].lastSeenMs) > RADAR_STALE_MS * 2) {
                ctx.targets[i].active = false;
            }
        }
    }
    portEXIT_CRITICAL(&ctx.mux);
}

static std::vector<RadarTarget> snapshotVisibleTargets(RadarContext &ctx, RadarMode mode) {
    std::vector<RadarTarget> list;
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        const RadarTarget &t = ctx.targets[i];
        if (!t.active) continue;
        if (mode == RadarMode::WIFI_ONLY && t.type != RadarTargetType::WIFI) continue;
        if (mode == RadarMode::BLE_ONLY && t.type != RadarTargetType::BLE) continue;
        list.push_back(t);
    }
    portEXIT_CRITICAL(&ctx.mux);
    std::sort(list.begin(), list.end(), [](const RadarTarget &a, const RadarTarget &b) {
        return a.distM < b.distM;
    });
    return list;
}

static void drawRadarScope(
    int cx, int cy, int r, float sweepRad, const std::vector<RadarTarget> &list, const RadarContext &ctx
) {
    const uint16_t ringCol = TFT_DARKGREEN;
    const uint16_t bg = bruceConfig.bgColor;

    // Clear scope disc cleanly
    tft.fillCircle(cx, cy, r + 1, bg);

    // Distance-calibrated concentric rings (2m, 8m, 30m) + crosshairs
    int r2 = distToRadius(2.0f, r);
    int r8 = distToRadius(8.0f, r);
    tft.drawCircle(cx, cy, r, ringCol);
    tft.drawCircle(cx, cy, r8, ringCol);
    tft.drawCircle(cx, cy, r2, ringCol);
    tft.drawFastHLine(cx - r, cy, 2 * r + 1, ringCol);
    tft.drawFastVLine(cx, cy - r, 2 * r + 1, ringCol);

    // Subtle distance ring labels on larger scopes
    if (r >= 52) {
        tft.setTextSize(1);
        tft.setTextColor(TFT_DARKGREEN, bg);
        tft.drawString("2m", cx + 2, cy - r2 + 1, 1);
        tft.drawString("8m", cx + 2, cy - r8 + 1, 1);
    }

    // Rotating sweep beam
    float trailRad = sweepRad - 0.14f;
    tft.drawLine(
        cx, cy, cx + (int)(cosf(trailRad) * (r - 2)), cy + (int)(sinf(trailRad) * (r - 2)), TFT_DARKGREEN
    );
    tft.drawLine(
        cx, cy, cx + (int)(cosf(sweepRad) * (r - 1)), cy + (int)(sinf(sweepRad) * (r - 1)), TFT_GREEN
    );

    struct LabelRect {
        int x, y, w, h;
    };
    LabelRect placed[5];
    int placedCount = 0;

    auto tryDrawCallout = [&](int bx, int by, const RadarTarget &t, uint16_t col, bool force) {
        if (!ctx.showLabels || r < 46) return;
        if (!force && placedCount >= 4) return;
        String tag = targetDisplayName(t);
        if ((int)tag.length() > 7) tag = tag.substring(0, 7);
        tag += " " + formatDistShort(t.distM);

        int tw = (int)tag.length() * LW;
        int th = LH;
        int lx = (bx + 5 + tw <= cx + r) ? (bx + 5) : (bx - tw - 4);
        int ly = constrain(by - 4, cy - r + 2, cy + r - th - 2);
        lx = constrain(lx, cx - r + 2, cx + r - tw - 2);

        for (int i = 0; i < placedCount; ++i) {
            bool overlap = !(lx + tw < placed[i].x || lx > placed[i].x + placed[i].w ||
                             ly + th < placed[i].y || ly > placed[i].y + placed[i].h);
            if (overlap) return;
        }
        if (placedCount < 5) placed[placedCount++] = {lx, ly, tw, th};
        tft.setTextSize(1);
        tft.setTextColor(col, bg);
        tft.drawString(tag, lx, ly, 1);
    };

    // Plot blips (farthest first so closest & locked stay on top)
    for (int i = (int)list.size() - 1; i >= 0; --i) {
        const RadarTarget &t = list[i];
        int distPx = distToRadius(t.distM, r);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);

        bool locked = ctx.hasLocked && t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0;
        uint16_t col = categoryColor(t.category);
        if (locked) {
            tft.drawLine(cx, cy, bx, by, TFT_YELLOW);
            tft.drawCircle(bx, by, 5, TFT_RED);
            tft.fillCircle(bx, by, 2, TFT_YELLOW);
            tryDrawCallout(bx, by, t, TFT_YELLOW, true);
        } else {
            drawCategoryGlyph(bx, by, t.category, col);
            if (i < 4) tryDrawCallout(bx, by, t, col, false);
        }
    }

    // Center scanner origin
    tft.fillCircle(cx, cy, 2, TFT_WHITE);
}

static void drawRadarHud(
    int hudX, int hudY, int hudW, int hudH, RadarMode mode, bool timeSliced, bool scanningWifi,
    bool scanningBle, const std::vector<RadarTarget> &list, const RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(hudX, hudY, hudW, hudH, bg);
    tft.setTextSize(FP);

    const int lineStep = (FP == 1 && bruce_i18n::isChinese()) ? 13 : (LH + 2);
    const int maxChars = max(6, hudW / LW);

    int wifiCnt = 0, bleCnt = 0, trackerCnt = 0;
    const RadarTarget *lockedTarget = nullptr;
    float anchorSumX = 0.0f, anchorSumY = 0.0f;
    int anchorCnt = 0;

    for (const auto &t : list) {
        if (t.type == RadarTargetType::WIFI) {
            ++wifiCnt;
        } else {
            ++bleCnt;
            if (t.category == RadarCategory::BLE_TRACKER) ++trackerCnt;
            if (anchorCnt < 6) {
                float w = 1.0f / max(0.5f, t.distM);
                anchorSumX += t.relX * w;
                anchorSumY += t.relY * w;
                ++anchorCnt;
            }
        }
        if (ctx.hasLocked && t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0) {
            lockedTarget = &t;
        }
    }

    int y = hudY;
    // Header: Mode + active radio + COEX/TS indicator
    tft.setTextColor(bruceConfig.priColor, bg);
    String radioBadge = scanningWifi ? "[W*]" : (scanningBle ? "[B*]" : "[..]");
    String hdr = String(modeShort(mode)) + " " + radioBadge + (timeSliced ? " TS" : " CX");
    tft.drawString(hdr, hudX, y, 1);
    y += LH + 2;

    // Live GPS or BLE-Anchor location status line
    tft.setTextColor(ctx.gpsFix ? TFT_GREEN : TFT_LIGHTGREY, bg);
    char locLine[32];
    if (ctx.gpsFix) {
        snprintf(locLine, sizeof(locLine), "%.4f,%.4f", ctx.gpsLat, ctx.gpsLon);
    } else if (anchorCnt > 0) {
        snprintf(
            locLine,
            sizeof(locLine),
            "BLE:%d (%.1f,%.1f)m",
            anchorCnt,
            anchorSumX / anchorCnt,
            anchorSumY / anchorCnt
        );
    } else {
        snprintf(locLine, sizeof(locLine), "Loc: scanning...");
    }
    String locStr(locLine);
    if ((int)locStr.length() > maxChars) locStr = locStr.substring(0, maxChars);
    tft.drawString(locStr, hudX, y, 1);
    y += LH + 2;

    // Compact legend + live counts
    tft.fillRect(hudX, y + 1, 5, 5, TFT_CYAN);
    tft.setTextColor(TFT_CYAN, bg);
    tft.drawString("W:" + String(wifiCnt), hudX + 7, y, 1);

    int col2 = hudX + hudW / 3 + 2;
    tft.fillCircle(col2 + 2, y + 3, 2, TFT_MAGENTA);
    tft.setTextColor(TFT_MAGENTA, bg);
    tft.drawString("B:" + String(bleCnt), col2 + 7, y, 1);

    if (hudW >= 120) {
        int col3 = hudX + (hudW * 2) / 3 + 2;
        tft.fillTriangle(col3 + 2, y, col3, y + 3, col3 + 4, y + 3, TFT_RED);
        tft.fillTriangle(col3 + 2, y + 6, col3, y + 3, col3 + 4, y + 3, TFT_RED);
        tft.setTextColor(TFT_RED, bg);
        tft.drawString("T:" + String(trackerCnt), col3 + 7, y, 1);
    }
    y += LH + 3;

    tft.drawFastHLine(hudX, y, hudW, bruceConfig.secColor);
    y += 4;

    if (ctx.hasLocked) {
        tft.setTextColor(TFT_YELLOW, bg);
        tft.drawString(bruce_i18n::tr("Lock Target"), hudX, y, 1);
        y += lineStep;

        if (lockedTarget) {
            uint16_t catCol = categoryColor(lockedTarget->category);
            drawCategoryGlyph(hudX + 3, y + 3, lockedTarget->category, catCol);
            tft.setTextColor(catCol, bg);
            String catTag = bruce_i18n::tr(categoryName(lockedTarget->category));
            catTag = bruce_i18n::utf8TruncateToWidth(catTag, hudW - 10, FP);
            tft.drawString(catTag, hudX + 9, y, 1);
            y += lineStep;

            String label = targetDisplayName(*lockedTarget);
            if ((int)label.length() > maxChars) label = label.substring(0, maxChars);
            tft.setTextColor(TFT_WHITE, bg);
            tft.drawString(label, hudX, y, 1);
            y += LH + 2;

            // Distance + RSSI
            int pct = constrain((lockedTarget->rssi + 95) * 100 / 60, 0, 100);
            uint16_t barCol = (pct > 66) ? TFT_GREEN : ((pct > 33) ? TFT_YELLOW : TFT_RED);
            tft.setTextColor(barCol, bg);
            tft.drawString(
                formatDistShort(lockedTarget->distM) + "  " + String(lockedTarget->rssi) + "dBm",
                hudX,
                y,
                1
            );
            y += LH + 2;

            // Relative (X, Y) coordinates + bearing if triangulated
            char coordBuf[32];
            if (lockedTarget->hasTriangulated) {
                float brg = vectorToBearingDeg(lockedTarget->relX, lockedTarget->relY);
                snprintf(
                    coordBuf,
                    sizeof(coordBuf),
                    "X%+.1f Y%+.1f %s",
                    lockedTarget->relX,
                    lockedTarget->relY,
                    bearingToCardinal(brg)
                );
            } else {
                snprintf(coordBuf, sizeof(coordBuf), "X%+.1f Y%+.1fm", lockedTarget->relX, lockedTarget->relY);
            }
            tft.setTextColor(TFT_LIGHTGREY, bg);
            tft.drawString(String(coordBuf).substring(0, maxChars), hudX, y, 1);
            y += LH + 3;

            // Proximity bar
            if (y + 10 <= hudY + hudH) {
                tft.drawRect(hudX, y, hudW - 2, 9, bruceConfig.secColor);
                int fillW = ((hudW - 4) * pct) / 100;
                if (fillW > 0) tft.fillRect(hudX + 1, y + 1, fillW, 7, barCol);
            }
        } else {
            tft.setTextColor(TFT_RED, bg);
            tft.drawString(bruce_i18n::tr("Lost signal..."), hudX, y, 1);
        }
        return;
    }

    // Closest devices list with category glyph, name/type, and distance in meters
    int rowH = LH + 2;
    int maxRows = max(1, (hudY + hudH - y - 2) / rowH);
    for (int i = 0; i < (int)list.size() && i < maxRows; ++i) {
        const RadarTarget &t = list[i];
        uint16_t col = categoryColor(t.category);
        drawCategoryGlyph(hudX + 3, y + 3, t.category, col);

        String distStr = formatDistShort(t.distM);
        int nameBudget = max(3, maxChars - (int)distStr.length() - 3);
        String nm = targetDisplayName(t);
        if ((int)nm.length() > nameBudget) nm = nm.substring(0, nameBudget);

        tft.setTextColor(col, bg);
        tft.drawString(nm, hudX + 9, y, 1);
        tft.setTextColor(TFT_LIGHTGREY, bg);
        tft.drawRightString(distStr, hudX + hudW - 1, y, 1);
        y += rowH;
    }
}

static void showDeviceInspector(RadarContext &ctx, const RadarTarget &t, bool &wantTriangulate) {
    drawMainBorderWithTitle("Device Info");
    const int x = BORDER_PAD_X + 6;
    int y = BORDER_PAD_Y + FM * LH + 6;
    const int rowH = (FP == 1 && bruce_i18n::isChinese()) ? 14 : (LH + 4);

    tft.setTextSize(FP);
    uint16_t col = categoryColor(t.category);
    drawCategoryGlyph(x + 3, y + 4, t.category, col);
    tft.setTextColor(col, bruceConfig.bgColor);
    String typeLine = String(t.type == RadarTargetType::WIFI ? "WiFi: " : "BLE: ") +
                      bruce_i18n::tr(categoryName(t.category));
    if (t.subtype[0]) typeLine += " (" + String(t.subtype) + ")";
    tft.drawString(typeLine, x + 10, y, 1);
    y += rowH;

    auto drawKV = [&](const char *k, const String &v, uint16_t vCol = TFT_WHITE) {
        if (y + rowH > tftHeight - BORDER_PAD_X - LH - 4) return;
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        String ks = String(bruce_i18n::tr(k)) + ": ";
        tft.drawString(ks, x, y, 1);
        int kw = bruce_i18n::utf8PixelWidth(ks, FP);
        tft.setTextColor(vCol, bruceConfig.bgColor);
        tft.drawString(v, x + kw, y, 1);
        y += rowH;
    };

    drawKV("Name", t.name[0] ? String(t.name) : String("<unnamed>"));
    drawKV("MAC", formatMac(t.mac), TFT_LIGHTGREY);

    String radioVal = String(t.rssi) + " dBm  (~" + formatDistShort(t.distM) + ")";
    if (t.type == RadarTargetType::WIFI) {
        radioVal += " CH" + String(t.channel) + " " + wifiAuthShort(t.authMode);
    } else {
        radioVal += " TX:" + String(t.refRssi1m);
    }
    drawKV("Signal", radioVal, TFT_GREEN);

    char coordBuf[40];
    if (t.hasTriangulated) {
        float brg = vectorToBearingDeg(t.relX, t.relY);
        snprintf(
            coordBuf,
            sizeof(coordBuf),
            "X:%+.1fm Y:%+.1fm (%.0f%s)",
            t.relX,
            t.relY,
            brg,
            bearingToCardinal(brg)
        );
    } else {
        snprintf(coordBuf, sizeof(coordBuf), "X:%+.1fm Y:%+.1fm (est)", t.relX, t.relY);
    }
    drawKV("Coords", String(coordBuf), TFT_YELLOW);

    if (t.hasGps) {
        char gpsBuf[36];
        snprintf(gpsBuf, sizeof(gpsBuf), "%.5f, %.5f", t.lat, t.lon);
        drawKV("GPS", String(gpsBuf), TFT_CYAN);
    }

    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(
        bruce_i18n::tr("SEL=Triangulate  UP=Lock  ESC=Back"),
        tftWidth / 2,
        tftHeight - BORDER_PAD_X - LH,
        1
    );

    check(SelPress);
    check(EscPress);
    check(UpPress);
    check(PrevPress);

    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (check(UpPress) || check(PrevPress)) {
            ctx.hasLocked = true;
            ctx.lockedType = t.type;
            memcpy(ctx.lockedMac, t.mac, 6);
            displaySuccess("Target Locked", true);
            break;
        }
        if (check(SelPress)) {
            ctx.hasLocked = true;
            ctx.lockedType = t.type;
            memcpy(ctx.lockedMac, t.mac, 6);
            wantTriangulate = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool pickTarget(
    RadarContext &ctx, const std::vector<RadarTarget> &list, const char *title, RadarTarget &outTarget
) {
    if (list.empty()) {
        displayWarning("No devices found", true);
        return false;
    }
    bool picked = false;
    options.clear();
    size_t limit = min<size_t>(list.size(), 30);
    for (size_t i = 0; i < limit; ++i) {
        const RadarTarget t = list[i];
        String prefix = (t.type == RadarTargetType::WIFI)
                            ? "[W] "
                            : ((t.category == RadarCategory::BLE_TRACKER) ? "[!] " : "[B] ");
        String label = prefix + targetDisplayName(t) + " " + formatDistShort(t.distM);
        options.push_back({label, [&outTarget, &picked, t]() {
            outTarget = t;
            picked = true;
        }});
    }
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, title);
    return picked;
}

static void exportRadarCsv(const std::vector<RadarTarget> &list) {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (checkLittleFsSize()) fs = &LittleFS;
    if (!fs) {
        displayError("No storage found", true);
        return;
    }

    File f = fs->open("/radar_log.csv", FILE_WRITE);
    if (!f) {
        displayError("Cannot write file", true);
        return;
    }
    f.println("ms,radio,category,subtype,name,mac,rssi,dist_m,rel_x_m,rel_y_m,ch,lat,lon");
    for (const auto &t : list) {
        f.printf(
            "%lu,%s,%s,%s,\"%s\",%s,%d,%.2f,%.2f,%.2f,%u,%.6f,%.6f\n",
            (unsigned long)t.lastSeenMs,
            t.type == RadarTargetType::WIFI ? "WIFI" : "BLE",
            categoryName(t.category),
            t.subtype,
            t.name,
            formatMac(t.mac).c_str(),
            (int)t.rssi,
            t.distM,
            t.relX,
            t.relY,
            (unsigned)t.channel,
            t.hasGps ? t.lat : 0.0,
            t.hasGps ? t.lon : 0.0
        );
    }
    f.close();
    displaySuccess(String("Saved ") + String(list.size()) + " -> /radar_log.csv", true);
}

// Measure average RSSI of a specific target at the current physical point
static bool sampleTargetDistance(
    RadarContext &ctx, const RadarTarget &target, bool timeSliced, bool &wifiAvailable, bool &bleAvailable,
    NimBLEScan *&radarScan, float &outDistM, int8_t &outAvgRssi, int &outNearbyBle
) {
    // Capture fresh samples for ~1.8s
    int32_t rssiSum = 0;
    int rssiCount = 0;

    if (target.type == RadarTargetType::WIFI) {
        if (timeSliced && (bleAvailable || NimBLEDevice::isInitialized())) {
            if (radarScan) {
                radarScan->stop();
                radarScan->setScanCallbacks(nullptr);
            }
            stopBLEStack();
            bleAvailable = false;
            radarScan = nullptr;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        if (WiFi.getMode() == WIFI_MODE_NULL) {
            WiFi.mode(WIFI_MODE_STA);
            vTaskDelay(pdMS_TO_TICKS(50));
            wifiAvailable = true;
        }
        for (int pass = 0; pass < 2; ++pass) {
            int16_t n = WiFi.scanNetworks(false, true, false, 110, target.channel);
            for (int16_t i = 0; i < n; ++i) {
                uint8_t *bssid = WiFi.BSSID(i);
                if (bssid && memcmp(bssid, target.mac, 6) == 0) {
                    rssiSum += WiFi.RSSI(i);
                    ++rssiCount;
                }
            }
            WiFi.scanDelete();
            if (check(EscPress)) return false;
        }
    } else {
        if (timeSliced && WiFi.getMode() != WIFI_MODE_NULL) {
            WiFi.scanDelete();
            wifiDisconnect();
            WiFi.mode(WIFI_OFF);
            esp_wifi_stop();
            esp_wifi_deinit();
            wifiAvailable = false;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        if (!NimBLEDevice::isInitialized()) {
            if (!radioSafeBleInit("", false)) return false;
        }
        radarScan = NimBLEDevice::getScan();
        if (!radarScan) return false;
        radarScan->setScanCallbacks(&s_radarBleCb, true);
        radarScan->setActiveScan(true);
        radarScan->setInterval(80);
        radarScan->setWindow(65);
        bleAvailable = true;

        uint32_t t0 = millis();
        radarScan->start(1800, false, true);
        uint32_t lastSeenCheck = 0;
        while (millis() - t0 < 1900) {
            if (check(EscPress)) {
                radarScan->stop();
                return false;
            }
            portENTER_CRITICAL(&ctx.mux);
            for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
                const RadarTarget &slot = ctx.targets[i];
                if (slot.active && slot.type == RadarTargetType::BLE &&
                    memcmp(slot.mac, target.mac, 6) == 0 && slot.lastSeenMs != lastSeenCheck) {
                    rssiSum += slot.rssi;
                    ++rssiCount;
                    lastSeenCheck = slot.lastSeenMs;
                }
            }
            portEXIT_CRITICAL(&ctx.mux);
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        radarScan->stop();
        radarScan->clearResults();
    }

    // Count active nearby BLE anchors for context
    outNearbyBle = 0;
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        if (ctx.targets[i].active && ctx.targets[i].type == RadarTargetType::BLE) ++outNearbyBle;
    }
    portEXIT_CRITICAL(&ctx.mux);

    if (rssiCount == 0) {
        outAvgRssi = target.rssi;
        outDistM = target.distM;
        return false;
    }
    outAvgRssi = (int8_t)(rssiSum / rssiCount);
    outDistM = estimateDistanceM(outAvgRssi, target.refRssi1m);
    return true;
}

static void runTriangulateWizard(
    RadarContext &ctx, const RadarTarget &target, bool timeSliced, bool &wifiAvailable, bool &bleAvailable,
    NimBLEScan *&radarScan
) {
    const float D = RADAR_TRIANGULATE_STEP_M;
    float r[3] = {target.distM, target.distM, target.distM};
    int8_t avgRssi[3] = {target.rssi, target.rssi, target.rssi};
    int bleAnchors = 0;

    const char *stepPrompts[3] = {
        "P1 (0,0): Stand at origin",
        "P2 (+2m,0): Move 2m RIGHT",
        "P3 (0,+2m): Move 2m FORWARD",
    };

    for (int step = 0; step < 3; ++step) {
        drawMainBorderWithTitle("Locate / Triangulate");
        int x = BORDER_PAD_X + 6;
        int y = BORDER_PAD_Y + FM * LH + 6;
        int stepH = (FP == 1 && bruce_i18n::isChinese()) ? 14 : (LH + 4);

        tft.setTextSize(FP);
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        tft.drawString(targetDisplayName(target) + " (" + formatMac(target.mac).substring(9) + ")", x, y, 1);
        y += stepH;

        tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
        tft.drawString(bruce_i18n::tr(stepPrompts[step]), x, y, 1);
        y += stepH + 2;

        tft.setTextColor(TFT_LIGHTGREY, bruceConfig.bgColor);
        for (int prev = 0; prev < step; ++prev) {
            char line[36];
            snprintf(line, sizeof(line), "P%d: %ddBm (~%.1fm)", prev + 1, (int)avgRssi[prev], r[prev]);
            tft.drawString(line, x, y, 1);
            y += LH + 3;
        }

        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.drawCentreString(
            bruce_i18n::tr("SEL=Sample Point  ESC=Cancel"),
            tftWidth / 2,
            tftHeight - BORDER_PAD_X - LH,
            1
        );

        check(SelPress);
        check(EscPress);
        while (!returnToMenu) {
            if (check(EscPress)) return;
            if (check(SelPress)) break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
        tft.drawCentreString(bruce_i18n::tr("Sampling signal..."), tftWidth / 2, y + 6, 1);
        bool gotSample = sampleTargetDistance(
            ctx, target, timeSliced, wifiAvailable, bleAvailable, radarScan, r[step], avgRssi[step], bleAnchors
        );
        if (!gotSample) {
            displayWarning("Weak signal: using last RSSI", true);
        }
    }

    // Solve 2D trilateration from P1(0,0), P2(D,0), P3(0,D):
    // x = (r1^2 - r2^2 + D^2) / (2D), y = (r1^2 - r3^2 + D^2) / (2D)
    float xEst = (r[0] * r[0] - r[1] * r[1] + D * D) / (2.0f * D);
    float yEst = (r[0] * r[0] - r[2] * r[2] + D * D) / (2.0f * D);
    float distEst = sqrtf(xEst * xEst + yEst * yEst);
    float brgDeg = vectorToBearingDeg(xEst, yEst);

    // Save triangulated coordinates back into the live Radar target
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        RadarTarget &slot = ctx.targets[i];
        if (slot.active && slot.type == target.type && memcmp(slot.mac, target.mac, 6) == 0) {
            slot.hasTriangulated = true;
            slot.relX = xEst;
            slot.relY = yEst;
            slot.distM = max(0.2f, distEst);
            slot.angleRad = atan2f(-yEst, xEst);
            break;
        }
    }
    portEXIT_CRITICAL(&ctx.mux);

    // Render visual trilateration result map
    drawMainBorderWithTitle("Triangulation Result");
    int top = BORDER_PAD_Y + FM * LH + 4;
    int bot = tftHeight - BORDER_PAD_X - LH - 4;
    int h = max(40, bot - top);
    int mapR = min(h / 2 - 4, (tftWidth / 2 - BORDER_PAD_X - 8) / 2);
    int mapCx = BORDER_PAD_X + mapR + 8;
    int mapCy = top + h / 2;

    float maxCoord = max(D + 1.0f, max(fabsf(xEst), fabsf(yEst)) * 1.25f);
    float scalePx = (float)(mapR - 4) / maxCoord;

    tft.drawCircle(mapCx, mapCy, mapR, TFT_DARKGREEN);
    tft.drawFastHLine(mapCx - mapR, mapCy, 2 * mapR + 1, TFT_DARKGREEN);
    tft.drawFastVLine(mapCx, mapCy - mapR, 2 * mapR + 1, TFT_DARKGREEN);

    // Plot sample points P1, P2, P3
    int p1x = mapCx, p1y = mapCy;
    int p2x = mapCx + (int)(D * scalePx), p2y = mapCy;
    int p3x = mapCx, p3y = mapCy - (int)(D * scalePx);
    tft.fillCircle(p1x, p1y, 2, TFT_CYAN);
    tft.fillCircle(p2x, p2y, 2, TFT_CYAN);
    tft.fillCircle(p3x, p3y, 2, TFT_CYAN);

    // Plot solved target vector
    int tx = constrain(mapCx + (int)(xEst * scalePx), mapCx - mapR + 3, mapCx + mapR - 3);
    int ty = constrain(mapCy - (int)(yEst * scalePx), mapCy - mapR + 3, mapCy + mapR - 3);
    tft.drawLine(p1x, p1y, tx, ty, TFT_YELLOW);
    tft.drawCircle(tx, ty, 5, TFT_RED);
    tft.fillCircle(tx, ty, 2, TFT_YELLOW);

    // Right info panel
    int ix = mapCx + mapR + 10;
    int iy = top + 2;
    int stepH = (FP == 1 && bruce_i18n::isChinese()) ? 13 : (LH + 3);
    tft.setTextSize(FP);

    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.drawString(targetDisplayName(target), ix, iy, 1);
    iy += LH + 3;

    char buf[36];
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    snprintf(buf, sizeof(buf), "Dist: %.1fm", distEst);
    tft.drawString(buf, ix, iy, 1);
    iy += LH + 3;

    tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
    snprintf(buf, sizeof(buf), "X:%+.1f Y:%+.1fm", xEst, yEst);
    tft.drawString(buf, ix, iy, 1);
    iy += LH + 3;

    tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
    snprintf(buf, sizeof(buf), "Brg: %.0f (%s)", brgDeg, bearingToCardinal(brgDeg));
    tft.drawString(buf, ix, iy, 1);
    iy += LH + 3;

    tft.setTextColor(TFT_LIGHTGREY, bruceConfig.bgColor);
    snprintf(buf, sizeof(buf), "BLE Ref: %d", bleAnchors);
    tft.drawString(buf, ix, iy, 1);
    iy += stepH;

    if (ctx.gpsFix) {
        snprintf(buf, sizeof(buf), "%.4f,%.4f", ctx.gpsLat, ctx.gpsLon);
        tft.drawString(buf, ix, iy, 1);
    }

    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(
        bruce_i18n::tr("Press any key to return"),
        tftWidth / 2,
        tftHeight - BORDER_PAD_X - LH,
        1
    );

    check(SelPress);
    check(EscPress);
    while (!returnToMenu) {
        if (check(SelPress) || check(EscPress)) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool gpsPinsSafeToOpen() {
    int rx = bruceConfigPins.gps_bus.rx;
    int tx = bruceConfigPins.gps_bus.tx;
    if (rx < 0 || tx < 0) return false;
    if (bruceConfigPins.SDCARD_bus.checkConflict(rx) || bruceConfigPins.SDCARD_bus.checkConflict(tx))
        return false;
    if (bruceConfigPins.CC1101_bus.checkConflict(rx) || bruceConfigPins.NRF24_bus.checkConflict(rx))
        return false;
    return true;
}

} // namespace

void radar_setup(RadarInitialMode initialMode) {
    RadarContext ctx;
    if (psramFound()) {
        ctx.targets = static_cast<RadarTarget *>(ps_calloc(RADAR_MAX_TARGETS, sizeof(RadarTarget)));
    }
    if (!ctx.targets) {
        ctx.targets = static_cast<RadarTarget *>(calloc(RADAR_MAX_TARGETS, sizeof(RadarTarget)));
    }
    if (!ctx.targets) {
        displayError("Out of memory", true);
        return;
    }
    s_radarCtx = &ctx;

    // Optional non-blocking GPS receiver if dedicated GPS UART pins are available
    TinyGPSPlus gpsParser;
    HardwareSerial gpsSerial(2);
    if (gpsPinsSafeToOpen()) {
        ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
        gpsSerial.begin(
            bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx, bruceConfigPins.gps_bus.tx
        );
        ctx.gpsStarted = true;
    }

    RadarMode mode = (initialMode == RADAR_INIT_WIFI)
                         ? RadarMode::WIFI_ONLY
                         : ((initialMode == RADAR_INIT_BLE) ? RadarMode::BLE_ONLY : RadarMode::BOTH);

    // Coexistence on PSRAM boards when DMA allows; automatic time-sliced hand-off on low-RAM boards
    bool timeSliced = !psramFound();
    bool wifiAvailable = (WiFi.getMode() != WIFI_MODE_NULL);
    bool bleAvailable = false;
    bool wifiScanning = false;
    bool bleScanning = false;
    bool nextIsBle = (mode == RadarMode::BLE_ONLY);
    uint32_t lastSweepEndMs = 0;
    float sweepRad = 0.0f;
    NimBLEScan *radarScan = nullptr;

    auto teardownBle = [&]() {
        if (bleScanning && radarScan) {
            radarScan->stop();
            bleScanning = false;
        }
        if (radarScan) {
            radarScan->setScanCallbacks(nullptr);
            radarScan->clearResults();
            radarScan = nullptr;
        }
        if (NimBLEDevice::isInitialized() || bleAvailable) {
            stopBLEStack();
            bleAvailable = false;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    };

    auto teardownWifi = [&]() {
        if (wifiScanning) {
            WiFi.scanDelete();
            wifiScanning = false;
        }
        if (WiFi.getMode() != WIFI_MODE_NULL || wifiAvailable) {
            WiFi.scanDelete();
            wifiDisconnect();
            WiFi.mode(WIFI_OFF);
            esp_wifi_stop();
            esp_wifi_deinit();
            wifiAvailable = false;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    };

    auto ensureWifiReady = [&]() -> bool {
        if (timeSliced && (bleAvailable || NimBLEDevice::isInitialized())) teardownBle();
        if (wifiAvailable && WiFi.getMode() != WIFI_MODE_NULL) return true;

        if (!radioHasMemForWifi(false)) {
            // Switch to time-sliced mode and reclaim BLE so we never pop up a Low RAM error
            timeSliced = true;
            teardownBle();
            if (!radioHasMemForWifi(true)) return false;
        }
        if (WiFi.getMode() == WIFI_MODE_NULL) {
            WiFi.mode(WIFI_STA);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        wifiAvailable = (WiFi.getMode() != WIFI_MODE_NULL);
        return wifiAvailable;
    };

    auto ensureBleReady = [&]() -> bool {
        if (timeSliced && (wifiAvailable || WiFi.getMode() != WIFI_MODE_NULL)) teardownWifi();
        if (bleAvailable && radarScan && NimBLEDevice::isInitialized()) return true;

        if (!NimBLEDevice::isInitialized()) {
            // If DMA is too tight while Wi-Fi is still up, switch to time-sliced cleanly
            size_t minBlk = 0, minFree = 0;
            radioBleMinThresholds(minBlk, minFree);
            if (!radioDmaAtLeast(minBlk, minFree) && WiFi.getMode() != WIFI_MODE_NULL) {
                timeSliced = true;
                teardownWifi();
            }
            if (!radioHasMemForBle(false) || !radioSafeBleInit("", false)) {
                timeSliced = true;
                teardownWifi();
                if (!radioSafeBleInit("", true)) return false;
            }
        }
        radarScan = NimBLEDevice::getScan();
        if (!radarScan) return false;
        radarScan->setScanCallbacks(&s_radarBleCb, true);
        radarScan->setActiveScan(true);
        radarScan->setInterval(97);
        radarScan->setWindow(67);
        bleAvailable = true;
        return true;
    };

    auto stopAllScans = [&]() {
        if (bleScanning && radarScan) {
            radarScan->stop();
            bleScanning = false;
        }
        if (wifiScanning) {
            WiFi.scanDelete();
            wifiScanning = false;
        }
    };

    auto drawLayout = [&]() {
        drawMainBorderWithTitle("Radar");
        tft.setTextSize(FP);
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawCentreString(
            bruce_i18n::tr("SEL=Menu  UP/DN=Filter  ESC=Back"),
            tftWidth / 2,
            tftHeight - BORDER_PAD_X - LH,
            1
        );
    };

    drawLayout();

    const int contentTop = BORDER_PAD_Y + FM * LH + 4;
    const int contentBot = tftHeight - BORDER_PAD_X - LH - 4;
    const int contentH = max(40, contentBot - contentTop);
    const int contentL = BORDER_PAD_X + 2;
    const int contentW = tftWidth - 2 * BORDER_PAD_X - 4;

    const bool wideLayout = (contentW >= contentH + 88);
    const int scopeR = wideLayout ? (min(contentH, contentW / 2) / 2 - 4) : (contentH / 2 - 4);
    const int scopeCx = wideLayout ? (contentL + scopeR + 4) : (tftWidth / 2);
    const int scopeCy = contentTop + contentH / 2;
    const int hudX = scopeCx + scopeR + 8;
    const int hudW = (contentL + contentW) - hudX;

    uint32_t lastFrameMs = 0;
    uint32_t lastHudMs = 0;
    bool exitRadar = false;

    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);

    while (!exitRadar && !returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();

        // 0) Non-blocking GPS NMEA feed
        if (ctx.gpsStarted) {
            int budget = 64;
            while (gpsSerial.available() > 0 && budget-- > 0) {
                gpsParser.encode(gpsSerial.read());
            }
            if (gpsParser.location.isValid() && gpsParser.location.age() < 5000) {
                ctx.gpsFix = true;
                ctx.gpsLat = gpsParser.location.lat();
                ctx.gpsLon = gpsParser.location.lng();
                ctx.gpsSats = gpsParser.satellites.value();
            } else {
                ctx.gpsFix = false;
            }
        }

        // 1) Collect completed Wi-Fi async scan
        if (wifiScanning) {
            int16_t n = WiFi.scanComplete();
            if (n >= 0) {
                for (int16_t i = 0; i < n; ++i) {
                    uint8_t *bssid = WiFi.BSSID(i);
                    if (!bssid) continue;
                    String ssid = WiFi.SSID(i);
                    uint8_t auth = (uint8_t)WiFi.encryptionType(i);
                    RadarCategory cat = ssid.isEmpty()
                                            ? RadarCategory::WIFI_HIDDEN
                                            : ((auth == WIFI_AUTH_OPEN) ? RadarCategory::WIFI_OPEN
                                                                        : RadarCategory::WIFI_SECURED);
                    radarUpsertTarget(
                        &ctx,
                        RadarTargetType::WIFI,
                        cat,
                        bssid,
                        ssid.c_str(),
                        wifiAuthShort(auth),
                        (int8_t)WiFi.RSSI(i),
                        -45,
                        (uint8_t)WiFi.channel(i),
                        auth
                    );
                }
                WiFi.scanDelete();
                wifiScanning = false;
                lastSweepEndMs = now;
                if (mode == RadarMode::BOTH) nextIsBle = true;
            } else if (n == WIFI_SCAN_FAILED) {
                WiFi.scanDelete();
                wifiScanning = false;
                lastSweepEndMs = now;
                if (mode == RadarMode::BOTH) nextIsBle = true;
            }
        }

        // 2) Check completed BLE scan
        if (bleScanning && radarScan && !radarScan->isScanning()) {
            radarScan->clearResults();
            bleScanning = false;
            lastSweepEndMs = now;
            if (mode == RadarMode::BOTH) nextIsBle = false;
        }

        // 3) Launch next sweep (coexisting or time-sliced seamlessly)
        if (!wifiScanning && !bleScanning && (now - lastSweepEndMs >= 220)) {
            bool wantBle = (mode == RadarMode::BLE_ONLY) || (mode == RadarMode::BOTH && nextIsBle);
            bool wantWifi = (mode == RadarMode::WIFI_ONLY) || (mode == RadarMode::BOTH && !nextIsBle);

            if (wantBle) {
                if (ensureBleReady() && radarScan->start(RADAR_BLE_SCAN_MS, false, true)) {
                    bleScanning = true;
                } else {
                    lastSweepEndMs = now;
                    if (mode == RadarMode::BOTH) nextIsBle = false;
                }
            } else if (wantWifi) {
                if (ensureWifiReady()) {
                    int16_t res = WiFi.scanNetworks(true, true, false, 110);
                    if (res == WIFI_SCAN_RUNNING) {
                        wifiScanning = true;
                    } else {
                        lastSweepEndMs = now;
                        if (mode == RadarMode::BOTH) nextIsBle = true;
                    }
                } else {
                    lastSweepEndMs = now;
                    if (mode == RadarMode::BOTH) nextIsBle = true;
                }
            }
        }

        // 4) Render scope (~16 FPS) & HUD (~3 FPS)
        if (now - lastFrameMs >= 60) {
            pruneStaleTargets(ctx);
            std::vector<RadarTarget> visible = snapshotVisibleTargets(ctx, mode);

            sweepRad += 0.16f;
            if (sweepRad >= 2.0f * RADAR_PI) sweepRad -= 2.0f * RADAR_PI;
            drawRadarScope(scopeCx, scopeCy, scopeR, sweepRad, visible, ctx);
            lastFrameMs = now;

            if (wideLayout && (now - lastHudMs >= 320 || lastHudMs == 0)) {
                drawRadarHud(
                    hudX, contentTop, hudW, contentH, mode, timeSliced, wifiScanning, bleScanning, visible, ctx
                );
                lastHudMs = now;
            }
        }

        // 5) Controls
        if (check(EscPress)) {
            exitRadar = true;
        } else if (check(PrevPress) || check(UpPress)) {
            stopAllScans();
            mode = (mode == RadarMode::BOTH)
                       ? RadarMode::BLE_ONLY
                       : ((mode == RadarMode::BLE_ONLY) ? RadarMode::WIFI_ONLY : RadarMode::BOTH);
            nextIsBle = (mode == RadarMode::BLE_ONLY);
            lastSweepEndMs = 0;
            lastHudMs = 0;
        } else if (check(NextPress) || check(DownPress)) {
            stopAllScans();
            mode = (mode == RadarMode::BOTH)
                       ? RadarMode::WIFI_ONLY
                       : ((mode == RadarMode::WIFI_ONLY) ? RadarMode::BLE_ONLY : RadarMode::BOTH);
            nextIsBle = (mode == RadarMode::BLE_ONLY);
            lastSweepEndMs = 0;
            lastHudMs = 0;
        } else if (check(SelPress)) {
            stopAllScans();
            std::vector<RadarTarget> visible = snapshotVisibleTargets(ctx, mode);
            options = {
                {"Inspect Target",
                 [&]() {
                     RadarTarget chosen;
                     if (pickTarget(ctx, visible, "Inspect Target", chosen)) {
                         bool wantTri = false;
                         showDeviceInspector(ctx, chosen, wantTri);
                         if (wantTri) {
                             runTriangulateWizard(
                                 ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan
                             );
                         }
                     }
                 }},
                {"Locate / Triangulate",
                 [&]() {
                     RadarTarget chosen;
                     bool have = false;
                     if (ctx.hasLocked) {
                         for (const auto &v : visible) {
                             if (v.type == ctx.lockedType && memcmp(v.mac, ctx.lockedMac, 6) == 0) {
                                 chosen = v;
                                 have = true;
                                 break;
                             }
                         }
                     }
                     if (!have) have = pickTarget(ctx, visible, "Locate / Triangulate", chosen);
                     if (have) {
                         ctx.hasLocked = true;
                         ctx.lockedType = chosen.type;
                         memcpy(ctx.lockedMac, chosen.mac, 6);
                         runTriangulateWizard(ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan);
                     }
                 }},
                {"Lock Target",
                 [&]() {
                     RadarTarget chosen;
                     if (pickTarget(ctx, visible, "Lock Target", chosen)) {
                         ctx.hasLocked = true;
                         ctx.lockedType = chosen.type;
                         memcpy(ctx.lockedMac, chosen.mac, 6);
                     }
                 }},
            };
            if (ctx.hasLocked) {
                options.push_back({"Unlock Target", [&]() { ctx.hasLocked = false; }});
            }
            options.push_back({modeLabel(mode), [&]() {
                mode = (mode == RadarMode::BOTH)
                           ? RadarMode::WIFI_ONLY
                           : ((mode == RadarMode::WIFI_ONLY) ? RadarMode::BLE_ONLY : RadarMode::BOTH);
                nextIsBle = (mode == RadarMode::BLE_ONLY);
            }});
            options.push_back({ctx.showLabels ? "Labels: ON" : "Labels: OFF", [&]() {
                ctx.showLabels = !ctx.showLabels;
            }});
            options.push_back({"Export CSV", [&]() { exportRadarCsv(visible); }});
            options.push_back({"Rescan Now", [&]() {
                portENTER_CRITICAL(&ctx.mux);
                for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) ctx.targets[i].active = false;
                portEXIT_CRITICAL(&ctx.mux);
                lastSweepEndMs = 0;
            }});
            options.push_back({"Exit", [&]() { exitRadar = true; }});
            loopOptions(options, MENU_TYPE_SUBMENU, "Radar");
            if (!exitRadar) {
                drawLayout();
                lastHudMs = 0;
                lastSweepEndMs = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Clean teardown
    stopAllScans();
    if (radarScan) {
        radarScan->setScanCallbacks(nullptr);
        radarScan->clearResults();
    }
    if (ctx.gpsStarted) {
        gpsSerial.end();
        ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
    }
    s_radarCtx = nullptr;
    free(ctx.targets);
    check(SelPress);
    check(EscPress);
}

void radar_locate_setup() {
    // Quick-entry Locate mode: do a fast BLE+WiFi discovery pass then jump right into Triangulate
    RadarContext ctx;
    if (psramFound()) {
        ctx.targets = static_cast<RadarTarget *>(ps_calloc(RADAR_MAX_TARGETS, sizeof(RadarTarget)));
    }
    if (!ctx.targets) {
        ctx.targets = static_cast<RadarTarget *>(calloc(RADAR_MAX_TARGETS, sizeof(RadarTarget)));
    }
    if (!ctx.targets) {
        displayError("Out of memory", true);
        return;
    }
    s_radarCtx = &ctx;

    bool timeSliced = !psramFound();
    bool wifiAvailable = (WiFi.getMode() != WIFI_MODE_NULL);
    bool bleAvailable = false;
    NimBLEScan *radarScan = nullptr;

    displayInfo("Scanning nearby BLE & WiFi...");

    // 1) BLE pass first (primary proximity / tracker beacons)
    if (timeSliced && WiFi.getMode() != WIFI_MODE_NULL) {
        WiFi.scanDelete();
        wifiDisconnect();
        WiFi.mode(WIFI_OFF);
        esp_wifi_stop();
        esp_wifi_deinit();
        wifiAvailable = false;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (NimBLEDevice::isInitialized() || (radioHasMemForBle(false) && radioSafeBleInit("", false))) {
        radarScan = NimBLEDevice::getScan();
        if (radarScan) {
            radarScan->setScanCallbacks(&s_radarBleCb, true);
            radarScan->setActiveScan(true);
            radarScan->setInterval(97);
            radarScan->setWindow(67);
            bleAvailable = true;
            radarScan->start(2200, false, true);
            uint32_t t0 = millis();
            while (millis() - t0 < 2300 && !check(EscPress)) vTaskDelay(pdMS_TO_TICKS(40));
            radarScan->stop();
            radarScan->clearResults();
        }
    }

    // 2) Wi-Fi pass
    if (timeSliced && (bleAvailable || NimBLEDevice::isInitialized())) {
        if (radarScan) {
            radarScan->stop();
            radarScan->setScanCallbacks(nullptr);
            radarScan = nullptr;
        }
        stopBLEStack();
        bleAvailable = false;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (radioHasMemForWifi(false)) {
        if (WiFi.getMode() == WIFI_MODE_NULL) {
            WiFi.mode(WIFI_STA);
            vTaskDelay(pdMS_TO_TICKS(50));
            wifiAvailable = true;
        }
        int16_t n = WiFi.scanNetworks(false, true, false, 110);
        for (int16_t i = 0; i < n; ++i) {
            uint8_t *bssid = WiFi.BSSID(i);
            if (!bssid) continue;
            String ssid = WiFi.SSID(i);
            uint8_t auth = (uint8_t)WiFi.encryptionType(i);
            RadarCategory cat = ssid.isEmpty()
                                    ? RadarCategory::WIFI_HIDDEN
                                    : ((auth == WIFI_AUTH_OPEN) ? RadarCategory::WIFI_OPEN
                                                                : RadarCategory::WIFI_SECURED);
            radarUpsertTarget(
                &ctx,
                RadarTargetType::WIFI,
                cat,
                bssid,
                ssid.c_str(),
                wifiAuthShort(auth),
                (int8_t)WiFi.RSSI(i),
                -45,
                (uint8_t)WiFi.channel(i),
                auth
            );
        }
        WiFi.scanDelete();
    }

    std::vector<RadarTarget> found = snapshotVisibleTargets(ctx, RadarMode::BOTH);
    RadarTarget chosen;
    if (pickTarget(ctx, found, "Locate / Triangulate", chosen)) {
        runTriangulateWizard(ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan);
    }

    if (radarScan) {
        radarScan->stop();
        radarScan->setScanCallbacks(nullptr);
        radarScan->clearResults();
    }
    s_radarCtx = nullptr;
    free(ctx.targets);
    check(SelPress);
    check(EscPress);
}

#endif // !LITE_VERSION
