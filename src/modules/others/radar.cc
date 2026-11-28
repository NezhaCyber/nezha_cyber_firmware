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
#include "core/location_state.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include "modules/ble/ble_common.h"
#include "modules/others/audio.h"
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
constexpr size_t RADAR_HIST_LEN = 56;
constexpr size_t RADAR_MAX_EVENTS = 14;
constexpr uint32_t RADAR_STALE_MS = 30000;
constexpr uint32_t RADAR_STALKER_MS = 45000;
constexpr uint32_t RADAR_BLE_SCAN_MS = 2100;
constexpr float RADAR_PI = 3.14159265f;
constexpr float RADAR_TRIANGULATE_STEP_M = 2.0f;

// Clean tactical radar palette (4-role semantic hierarchy, calm dark dish)
constexpr uint16_t COL_SCOPE_BG = 0x0821;      // Calm deep obsidian dish
constexpr uint16_t COL_N2_SCOPE_BG = 0x0062;   // Calm deep abyssal dish
constexpr uint16_t COL_GRID_DIM = 0x2104;      // Subtle neutral-warm range ring / crosshair
constexpr uint16_t COL_GRID_MID = 0x4228;      // Subtle outer bezel & muted labels
constexpr uint16_t COL_N2_GRID_DIM = 0x0944;   // Nezha2 subtle sonar ring
constexpr uint16_t COL_N2_GRID_MID = 0x1A68;   // Nezha2 subtle bezel & muted labels
constexpr uint16_t COL_QIANKUN_GOLD = 0xFEA0;  // Focused / locked reticle & highlight accent
constexpr uint16_t COL_SWEEP_F0 = 0x1041;      // Faint trailing sweep glow 0
constexpr uint16_t COL_SWEEP_F1 = 0x28A2;      // Faint trailing sweep glow 1
constexpr uint16_t COL_SWEEP_LEAD = 0x9B86;    // Crisp restrained leading beam
constexpr uint16_t COL_N2_SWEEP_F0 = 0x00E3;   // Nezha2 faint sonar trail 0
constexpr uint16_t COL_N2_SWEEP_F1 = 0x0226;   // Nezha2 faint sonar trail 1
constexpr uint16_t COL_N2_SWEEP_LEAD = 0x3D55; // Nezha2 crisp restrained beam
constexpr uint16_t COL_TAC_GREEN = 0x2F49;     // Open Wi-Fi / clear status green
constexpr uint16_t COL_BLE_NORM = 0x5DDF;      // Calm unified sky-blue for normal BLE devices
constexpr uint16_t COL_WEAK_DOT = 0x39E7;      // Dim muted speck for weak unnamed noise

// Theme-derived chrome colors so Radar HUD/tables/softkeys adapt to all 24 Nezha1/Nezha2 themes
static inline uint16_t radarPanelBg() { return uiSurfaceColor(); }
static inline uint16_t radarHeaderBg() { return uiSurfaceHiColor(); }
static inline uint16_t radarRowSelBg() { return uiSurfaceHiColor(); }
#define COL_PANEL_BG (radarPanelBg())
#define COL_HEADER_BG (radarHeaderBg())
#define COL_ROW_SEL (radarRowSelBg())

// Pentest & Military EW threat/alert bitmask
constexpr uint8_t RFLAG_TRACKER = 0x01;   // AirTag / SmartTag / Tile / iBeacon / Eddystone
constexpr uint8_t RFLAG_EVIL_TWIN = 0x02; // Duplicate SSID across distinct BSSIDs
constexpr uint8_t RFLAG_HACK_DEV = 0x04;  // Flipper Zero / Pwnagotchi / Hak5 / Marauder
constexpr uint8_t RFLAG_WEAK_SEC = 0x08;  // Open / WEP Wi-Fi
constexpr uint8_t RFLAG_DRONE = 0x10;     // DJI / Autel / Parrot UAV drone OUI or SSID
constexpr uint8_t RFLAG_STALKER = 0x20;   // Persistent close-range follower (<12m for >45s)

enum class RadarMode : uint8_t { BOTH = 0, WIFI_ONLY = 1, BLE_ONLY = 2, ALERTS_ONLY = 3, OPEN_WIFI = 4 };
enum class RadarView : uint8_t { SCOPE = 0, TABLE = 1, FOX_SPEC = 2, THREAT = 3 };
enum class RadarTargetType : uint8_t { WIFI = 0, BLE = 1 };

enum class RadarCategory : uint8_t {
    WIFI_OPEN = 0,
    WIFI_SECURED = 1,
    WIFI_HIDDEN = 2,
    WIFI_WEP = 3,
    BLE_TRACKER = 4,
    BLE_HACK = 5, // Flipper Zero / Pwnagotchi / Hak5
    BLE_APPLE = 6,
    BLE_SAMSUNG = 7,
    BLE_GOOGLE = 8,
    BLE_MICROSOFT = 9,
    BLE_AUDIO_HID = 10,
    BLE_GENERIC = 11,
};

struct OuiEntry {
    uint8_t b0, b1, b2;
    const char *vendor;
};

// Expanded tactical OUI vendor table in const flash (.rodata) — UAVs, IP cams, RF tools, mobile
static const OuiEntry kOuiTable[] = {
    {0x24, 0x0A, 0xC4, "Espressif"}, {0x30, 0xAE, 0xA4, "Espressif"}, {0xA4, 0xCF, 0x12, "Espressif"},
    {0xDC, 0xA6, 0x32, "RaspPi"   }, {0xB8, 0x27, 0xEB, "RaspPi"   }, {0xE4, 0x5F, 0x01, "RaspPi"   },
    {0xD8, 0x3A, 0xDD, "RaspPi"   }, {0x74, 0x83, 0xC2, "Ubiquiti" }, {0x80, 0x2A, 0xA8, "Ubiquiti" },
    {0xF0, 0x9F, 0xC2, "Ubiquiti" }, {0x00, 0x1A, 0x2B, "Cisco"    }, {0x00, 0x1B, 0xD5, "Cisco"    },
    {0x50, 0xC7, 0xBF, "TP-Link"  }, {0x98, 0xDA, 0xC4, "TP-Link"  }, {0xC0, 0x25, 0xE9, "TP-Link"  },
    {0xD4, 0xCA, 0x6D, "MikroTik" }, {0x4C, 0x5E, 0x0C, "MikroTik" }, {0x04, 0xD4, 0xC4, "Asus"     },
    {0x2C, 0xFD, 0xA1, "Asus"     }, {0x28, 0x6C, 0x07, "Xiaomi"   }, {0x64, 0x09, 0x80, "Xiaomi"   },
    {0x00, 0xE0, 0xFC, "Huawei"   }, {0x48, 0x46, 0xFB, "Huawei"   }, {0x3C, 0x22, 0xFB, "Apple"    },
    {0xAC, 0xBC, 0x32, "Apple"    }, {0xF0, 0x18, 0x98, "Apple"    }, {0x00, 0x12, 0xFB, "Samsung"  },
    {0x5C, 0x0A, 0x5B, "Samsung"  }, {0xF4, 0xF5, 0xD8, "Google"   }, {0x3C, 0x5A, 0xB4, "Google"   },
    {0x20, 0xF8, 0x5E, "Netgear"  }, {0xA0, 0x40, 0xA0, "Netgear"  }, {0x60, 0x60, 0x1F, "DJI"      },
    {0x34, 0xD2, 0x62, "DJI"      }, {0x48, 0x1C, 0xB9, "DJI"      }, {0xEC, 0x5B, 0xCD, "Autel"    },
    {0x90, 0x03, 0xB7, "Parrot"   }, {0xA0, 0x14, 0x3D, "Parrot"   }, {0x44, 0x19, 0xB6, "Hikvisn"  },
    {0xBC, 0xAD, 0x28, "Hikvisn"  }, {0x3C, 0xEF, 0x8C, "Dahua"    }, {0x00, 0x40, 0x8C, "AxisCam"  },
    {0x80, 0xE1, 0x26, "Flipper"  }, {0xDE, 0xAD, 0xBE, "Pwnagtch" }, {0x00, 0x13, 0x37, "Hak5"     },
    {0x4C, 0xFC, 0xAA, "Tesla"    }, {0x90, 0x84, 0x0D, "Apple"    }, {0x00, 0x17, 0x88, "Hue"      },
    {0x38, 0xF7, 0x3D, "Amazon"   },
};

static const char *lookupOuiVendor(const uint8_t mac[6]) {
    for (size_t i = 0; i < sizeof(kOuiTable) / sizeof(kOuiTable[0]); ++i) {
        if (kOuiTable[i].b0 == mac[0] && kOuiTable[i].b1 == mac[1] && kOuiTable[i].b2 == mac[2]) {
            return kOuiTable[i].vendor;
        }
    }
    return nullptr;
}

static bool isDroneVendorOrName(const char *oui, const char *name) {
    if (oui && (strcmp(oui, "DJI") == 0 || strcmp(oui, "Autel") == 0 || strcmp(oui, "Parrot") == 0)) {
        return true;
    }
    if (name && name[0]) {
        if (strstr(name, "DJI") || strstr(name, "Mavic") || strstr(name, "TELLO") ||
            strstr(name, "Parrot") || strstr(name, "Autel") || strstr(name, "Skydio")) {
            return true;
        }
    }
    return false;
}

struct RadarEvent {
    uint32_t tsMs = 0;
    uint16_t col = TFT_WHITE;
    char text[32] = {0};
};

struct RadarTarget {
    bool active = false;
    RadarTargetType type = RadarTargetType::WIFI;
    RadarCategory category = RadarCategory::BLE_GENERIC;
    uint8_t flags = 0;
    uint8_t loggedFlags = 0;
    uint8_t mac[6] = {0};
    char name[22] = {0};
    char subtype[14] = {0};
    int8_t rssi = -100;
    int8_t prevRssi = -100;
    int8_t peakRssi = -100;
    int8_t refRssi1m = -59;
    uint8_t channel = 0;
    uint8_t authMode = 0;
    uint16_t seenCount = 0;
    float angleRad = 0.0f;
    float distM = 10.0f;
    bool hasTriangulated = false;
    float relX = 0.0f;
    float relY = 0.0f;
    bool hasGps = false;
    double lat = 0.0;
    double lon = 0.0;
    uint32_t firstSeenMs = 0;
    uint32_t lastSeenMs = 0;
};

struct RadarContext {
    RadarTarget *targets = nullptr;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    bool hasLocked = false;
    uint8_t lockedMac[6] = {0};
    RadarTargetType lockedType = RadarTargetType::WIFI;
    bool hasFocused = false;
    uint8_t focusedMac[6] = {0};
    RadarTargetType focusedType = RadarTargetType::WIFI;
    bool showLabels = true;
    bool noiseFilter = true; // Suppress unnamed weak (< -80 dBm) generic BLE background noise
    bool geigerAudio = false;
    float rangeMaxM = 30.0f; // Real scope distance filter & zoom: 10m / 30m / 60m
    uint32_t startMs = 0;
    int scopeCursor = 0;
    int scopeScroll = 0;
    int tableCursor = 0;
    int tableScroll = 0;
    int alertCursor = 0;
    int alertScroll = 0;

    // Rolling RSSI sparkline history for Fox-Hunt mode
    int8_t foxHist[RADAR_HIST_LEN] = {0};
    size_t foxHistCount = 0;
    int8_t foxPeakRssi = -100;
    uint32_t lastGeigerMs = 0;

    // Bounded security alert event log ring buffer (threats only)
    RadarEvent events[RADAR_MAX_EVENTS];
    size_t eventHead = 0;
    size_t eventCount = 0;

    // Optional live GPS state
    bool gpsStarted = false;
    bool gpsFix = false;
    double gpsLat = 0.0;
    double gpsLon = 0.0;
    uint32_t gpsSats = 0;
};

static RadarContext *s_radarCtx = nullptr;

static void radarLogEvent(RadarContext &ctx, uint16_t col, const char *msg) {
    if (!msg || !msg[0]) return;
    RadarEvent &e = ctx.events[ctx.eventHead];
    e.tsMs = millis();
    e.col = col;
    strncpy(e.text, msg, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    ctx.eventHead = (ctx.eventHead + 1) % RADAR_MAX_EVENTS;
    if (ctx.eventCount < RADAR_MAX_EVENTS) ctx.eventCount++;
}

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

static String formatDwell(uint32_t firstMs, uint32_t nowMs) {
    uint32_t sec = (nowMs >= firstMs) ? (nowMs - firstMs) / 1000u : 0;
    char buf[10];
    if (sec < 60) snprintf(buf, sizeof(buf), "%lus", (unsigned long)sec);
    else snprintf(buf, sizeof(buf), "%lum", (unsigned long)(sec / 60));
    return String(buf);
}

static const char *bearingToCardinal(float deg) {
    static const char *dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)floorf((deg + 22.5f) / 45.0f) & 7;
    return dirs[idx];
}

static float vectorToBearingDeg(float dx, float dy) {
    float deg = atan2f(dx, dy) * (180.0f / RADAR_PI);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

constexpr uint8_t RFLAG_ANY_ALERT =
    RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV | RFLAG_DRONE | RFLAG_STALKER;

static bool isWeakUnnamedBle(const RadarTarget &t) {
    return t.type == RadarTargetType::BLE && t.category == RadarCategory::BLE_GENERIC &&
           t.flags == 0 && t.name[0] == '\0' && t.subtype[0] == '\0' && t.rssi < -80;
}

static bool isSameTarget(const RadarTarget &t, RadarTargetType type, const uint8_t mac[6]) {
    return t.type == type && memcmp(t.mac, mac, 6) == 0;
}

static uint16_t categoryColor(RadarCategory cat, uint8_t flags = 0) {
    if (flags & (RFLAG_EVIL_TWIN | RFLAG_TRACKER | RFLAG_STALKER)) return TFT_RED;
    if (flags & (RFLAG_DRONE | RFLAG_HACK_DEV)) return TFT_ORANGE;
    switch (cat) {
        case RadarCategory::WIFI_OPEN: return COL_TAC_GREEN;
        case RadarCategory::WIFI_WEP: return TFT_RED;
        case RadarCategory::WIFI_SECURED: return TFT_CYAN;
        case RadarCategory::WIFI_HIDDEN: return TFT_YELLOW;
        case RadarCategory::BLE_TRACKER: return TFT_RED;
        case RadarCategory::BLE_HACK: return TFT_ORANGE;
        default: return COL_BLE_NORM;
    }
}

static const char *alertBadgeShort(uint8_t flags) {
    if (flags & RFLAG_EVIL_TWIN) return "TWIN";
    if (flags & RFLAG_DRONE) return "UAV";
    if (flags & RFLAG_HACK_DEV) return "TOOL";
    if (flags & RFLAG_STALKER) return "STLK";
    if (flags & RFLAG_TRACKER) return "TRK";
    return nullptr;
}

static const char *alertReasonText(uint8_t flags) {
    if (flags & RFLAG_EVIL_TWIN) return "Evil Twin: open+enc duplicate SSID";
    if (flags & RFLAG_DRONE) return "Drone / UAV beacon detected";
    if (flags & RFLAG_HACK_DEV) return "RF pentest tool nearby";
    if (flags & RFLAG_STALKER) return "Close BLE device >45s";
    if (flags & RFLAG_TRACKER) return "Tracker / AirTag beacon nearby";
    if (flags & RFLAG_WEAK_SEC) return "Open or WEP wireless network";
    return "";
}

static int targetPriorityScore(const RadarTarget &t) {
    int sc = 0;
    if (t.flags & RFLAG_EVIL_TWIN) sc += 500;
    if (t.flags & RFLAG_DRONE) sc += 400;
    if (t.flags & RFLAG_HACK_DEV) sc += 320;
    if (t.flags & RFLAG_STALKER) sc += 260;
    if (t.flags & RFLAG_TRACKER) sc += 220;
    return sc;
}

static const char *categoryName(RadarCategory cat) {
    switch (cat) {
        case RadarCategory::WIFI_OPEN: return "Open WiFi";
        case RadarCategory::WIFI_WEP: return "WEP WiFi";
        case RadarCategory::WIFI_SECURED: return "Secured WiFi";
        case RadarCategory::WIFI_HIDDEN: return "Hidden AP";
        case RadarCategory::BLE_TRACKER: return "Tracker/Beacon";
        case RadarCategory::BLE_HACK: return "RF Tool / Hack";
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
    const char *oui = lookupOuiVendor(t.mac);
    if (oui) return String(oui);
    return formatMac(t.mac).substring(9);
}

static String targetShortTag(const RadarTarget &t) {
    if (t.flags & RFLAG_EVIL_TWIN) return "TWIN";
    if (t.flags & RFLAG_DRONE) return "DRN";
    if (t.flags & RFLAG_STALKER) return "STLK";
    if (t.flags & RFLAG_HACK_DEV) return "HACK";
    if (t.type == RadarTargetType::WIFI) {
        char buf[10];
        snprintf(buf, sizeof(buf), "C%u %s", (unsigned)t.channel, wifiAuthShort(t.authMode));
        return String(buf);
    }
    if (t.subtype[0]) return String(t.subtype);
    const char *oui = lookupOuiVendor(t.mac);
    return oui ? String(oui) : String("BLE");
}

template <typename Canvas>
static void drawCategoryGlyphTo(Canvas &c, int x, int y, RadarCategory cat, uint16_t col, uint8_t flags = 0) {
    // 3 clear, scannable shapes: Diamond = Alert/Threat, Square = Wi-Fi, Circle = BLE
    if (flags & RFLAG_ANY_ALERT) {
        c.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
        c.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
        if (flags & (RFLAG_EVIL_TWIN | RFLAG_DRONE | RFLAG_STALKER)) {
            c.drawCircle(x, y, 4, col);
        }
        return;
    }
    switch (cat) {
        case RadarCategory::WIFI_OPEN:
        case RadarCategory::WIFI_WEP:
        case RadarCategory::WIFI_SECURED:
            c.fillRect(x - 2, y - 2, 5, 5, col);
            break;
        case RadarCategory::WIFI_HIDDEN:
            c.drawRect(x - 2, y - 2, 5, 5, col);
            c.drawPixel(x, y, col);
            break;
        case RadarCategory::BLE_TRACKER:
        case RadarCategory::BLE_HACK:
            c.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
            c.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
            break;
        default:
            c.fillCircle(x, y, 2, col);
            break;
    }
}

static void drawCategoryGlyph(int x, int y, RadarCategory cat, uint16_t col, uint8_t flags = 0) {
    drawCategoryGlyphTo(tft, x, y, cat, col, flags);
}

static void classifyBleDevice(
    const NimBLEAdvertisedDevice *dev, const uint8_t mac[6], const std::string &nm, RadarCategory &cat,
    uint8_t &flags, char *subtypeBuf, size_t subtypeSz, int8_t &ref1m
) {
    cat = RadarCategory::BLE_GENERIC;
    flags = 0;
    subtypeBuf[0] = '\0';
    ref1m = -59;

    if (dev->haveTXPower()) {
        int8_t tx = dev->getTXPower();
        if (tx >= -20 && tx <= 20) ref1m = (int8_t)constrain((int)tx - 41, -78, -35);
    }

    // Check known RF/pentest hardware & UAV drones by name or OUI
    if (!nm.empty()) {
        if (nm.find("Flipper") != std::string::npos) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Flipper", subtypeSz - 1);
        } else if (nm.find("pwn") != std::string::npos || nm.find("Pwn") != std::string::npos) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Pwnagotchi", subtypeSz - 1);
        } else if (nm.find("Marauder") != std::string::npos || nm.find("Hak5") != std::string::npos) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "RF-Tool", subtypeSz - 1);
        }
    }
    const char *oui = lookupOuiVendor(mac);
    if (oui) {
        if (strcmp(oui, "Flipper") == 0) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Flipper", subtypeSz - 1);
        } else if (strcmp(oui, "Pwnagtch") == 0) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Pwnagotchi", subtypeSz - 1);
        } else if (strcmp(oui, "Hak5") == 0) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Hak5", subtypeSz - 1);
        } else if (!subtypeBuf[0]) {
            strncpy(subtypeBuf, oui, subtypeSz - 1);
        }
    }
    if (isDroneVendorOrName(oui, nm.empty() ? nullptr : nm.c_str())) {
        flags |= RFLAG_DRONE;
        if (!subtypeBuf[0]) strncpy(subtypeBuf, oui ? oui : "UAV", subtypeSz - 1);
    }

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
                        flags |= RFLAG_TRACKER;
                        strncpy(subtypeBuf, "iBeacon", subtypeSz - 1);
                        int8_t cal = (int8_t)mfg[24];
                        if (cal >= -90 && cal <= -30) ref1m = cal;
                    } else if (appleType == 0x12) {
                        cat = RadarCategory::BLE_TRACKER;
                        flags |= RFLAG_TRACKER;
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

    auto checkUuid = [&](const NimBLEUUID &u) {
        if (u.bitSize() != 16) return;
        std::string s = u.toString();
        unsigned int val = 0;
        if (sscanf(s.c_str(), "0x%x", &val) != 1 && sscanf(s.c_str(), "%x", &val) != 1) return;
        switch (val) {
            case 0x3080:
            case 0x3081:
            case 0x3082:
            case 0x3083:
                cat = RadarCategory::BLE_HACK;
                flags |= RFLAG_HACK_DEV;
                strncpy(subtypeBuf, "Flipper", subtypeSz - 1);
                break;
            case 0xFD5A:
                cat = RadarCategory::BLE_TRACKER;
                flags |= RFLAG_TRACKER;
                strncpy(subtypeBuf, "SmartTag", subtypeSz - 1);
                break;
            case 0xFEED:
                cat = RadarCategory::BLE_TRACKER;
                flags |= RFLAG_TRACKER;
                strncpy(subtypeBuf, "Tile", subtypeSz - 1);
                break;
            case 0xFEAA:
                cat = RadarCategory::BLE_TRACKER;
                flags |= RFLAG_TRACKER;
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
    RadarContext *ctx, RadarTargetType type, RadarCategory cat, uint8_t flags, const uint8_t mac[6],
    const char *name, const char *subtype, int8_t rssi, int8_t ref1m, uint8_t channel, uint8_t authMode
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
        slot.prevRssi = slot.rssi;
        slot.rssi = (int8_t)((slot.rssi * 3 + rssi * 5) / 8);
        if (rssi > slot.peakRssi) slot.peakRssi = rssi;
        if (slot.seenCount < 65535) ++slot.seenCount;
        slot.flags |= flags;
        if (cat != RadarCategory::BLE_GENERIC || slot.category == RadarCategory::BLE_GENERIC) {
            slot.category = cat;
        }
    } else {
        slot = RadarTarget{};
        slot.active = true;
        slot.type = type;
        slot.category = cat;
        slot.flags = flags;
        memcpy(slot.mac, mac, 6);
        slot.rssi = rssi;
        slot.prevRssi = rssi;
        slot.peakRssi = rssi;
        slot.seenCount = 1;
        slot.firstSeenMs = now;
        // Map Wi-Fi targets loosely by channel sector so channel clusters stand out on scope
        if (type == RadarTargetType::WIFI && channel >= 1 && channel <= 14) {
            float chBase = ((float)(channel - 1) / 14.0f) * 2.0f * RADAR_PI;
            float jitter = fmodf(macToAngleRad(mac), (2.0f * RADAR_PI / 14.0f));
            slot.angleRad = chBase + jitter;
        } else {
            slot.angleRad = macToAngleRad(mac);
        }
    }

    slot.refRssi1m = ref1m;
    slot.distM = estimateDistanceM(slot.rssi, slot.refRssi1m);
    if (!slot.hasTriangulated) {
        slot.relX = slot.distM * cosf(slot.angleRad);
        slot.relY = -slot.distM * sinf(slot.angleRad);
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

    // Drone/UAV detection (both Wi-Fi & BLE)
    const char *oui = lookupOuiVendor(slot.mac);
    if (isDroneVendorOrName(oui, slot.name)) slot.flags |= RFLAG_DRONE;

    // Persistent close-range stalker detection (<12m continuously for >45s on BLE or trackers)
    if (matchIdx >= 0 && slot.type == RadarTargetType::BLE &&
        (now - slot.firstSeenMs) >= RADAR_STALKER_MS && slot.distM <= 12.0f && slot.seenCount >= 4) {
        slot.flags |= RFLAG_STALKER;
    }

    // Capture newly triggered high-priority threat flags for security event logging
    uint8_t newlyLogged = (slot.flags & (RFLAG_DRONE | RFLAG_STALKER | RFLAG_HACK_DEV | RFLAG_TRACKER)) &
                          ~slot.loggedFlags;
    char logLabel[22] = {0};
    if (newlyLogged) {
        slot.loggedFlags |= newlyLogged;
        if (slot.name[0]) strncpy(logLabel, slot.name, sizeof(logLabel) - 1);
        else if (slot.subtype[0]) strncpy(logLabel, slot.subtype, sizeof(logLabel) - 1);
        else if (oui) strncpy(logLabel, oui, sizeof(logLabel) - 1);
        else snprintf(logLabel, sizeof(logLabel), "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
    }

    double estLat = 0.0, estLon = 0.0;
    if (bruceLocation.estimateTargetGps(slot.relX, slot.relY, estLat, estLon)) {
        slot.hasGps = true;
        slot.lat = estLat;
        slot.lon = estLon;
    } else if (ctx->gpsFix) {
        slot.hasGps = true;
        slot.lat = ctx->gpsLat;
        slot.lon = ctx->gpsLon;
    }
    slot.lastSeenMs = now;

    portEXIT_CRITICAL(&ctx->mux);

    if (newlyLogged) {
        char evBuf[32];
        if (newlyLogged & RFLAG_DRONE) {
            snprintf(evBuf, sizeof(evBuf), "UAV: %.22s", logLabel);
            radarLogEvent(*ctx, TFT_ORANGE, evBuf);
        } else if (newlyLogged & RFLAG_HACK_DEV) {
            snprintf(evBuf, sizeof(evBuf), "RF-TOOL: %.18s", logLabel);
            radarLogEvent(*ctx, TFT_ORANGE, evBuf);
        } else if (newlyLogged & RFLAG_STALKER) {
            snprintf(evBuf, sizeof(evBuf), "STALKER: %.18s", logLabel);
            radarLogEvent(*ctx, TFT_MAGENTA, evBuf);
        } else if (newlyLogged & RFLAG_TRACKER) {
            snprintf(evBuf, sizeof(evBuf), "TRACKER: %.18s", logLabel);
            radarLogEvent(*ctx, TFT_RED, evBuf);
        }
    }
}

// Flag Wi-Fi Evil-Twin / duplicate SSID anomalies (same SSID with mismatched encryption!)
static void detectEvilTwins(RadarContext &ctx) {
    char newTwinSsid[22] = {0};
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        ctx.targets[i].flags &= ~RFLAG_EVIL_TWIN;
    }
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        RadarTarget &a = ctx.targets[i];
        if (!a.active || a.type != RadarTargetType::WIFI || a.name[0] == '\0') continue;
        for (size_t j = i + 1; j < RADAR_MAX_TARGETS; ++j) {
            RadarTarget &b = ctx.targets[j];
            if (!b.active || b.type != RadarTargetType::WIFI || b.name[0] == '\0') continue;
            if (strcmp(a.name, b.name) == 0 && memcmp(a.mac, b.mac, 6) != 0) {
                // Flag when one is OPEN while the other is encrypted (classic Evil Twin / Karma signature)
                bool authMismatch =
                    (a.authMode == WIFI_AUTH_OPEN) != (b.authMode == WIFI_AUTH_OPEN);
                if (authMismatch) {
                    a.flags |= RFLAG_EVIL_TWIN;
                    b.flags |= RFLAG_EVIL_TWIN;
                    if (!(a.loggedFlags & RFLAG_EVIL_TWIN)) {
                        a.loggedFlags |= RFLAG_EVIL_TWIN;
                        b.loggedFlags |= RFLAG_EVIL_TWIN;
                        strncpy(newTwinSsid, a.name, sizeof(newTwinSsid) - 1);
                    }
                }
            }
        }
    }
    portEXIT_CRITICAL(&ctx.mux);
    if (newTwinSsid[0]) {
        char evBuf[32];
        snprintf(evBuf, sizeof(evBuf), "EVIL-TWIN: %.17s", newTwinSsid);
        radarLogEvent(ctx, TFT_RED, evBuf);
    }
}

class RadarBleCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!dev || !s_radarCtx) return;
        const NimBLEAddress addr = dev->getAddress();
        const uint8_t *raw = addr.getVal();
        uint8_t mac[6];
        for (int i = 0; i < 6; ++i) mac[i] = raw[5 - i];

        std::string nm = dev->getName();
        RadarCategory cat = RadarCategory::BLE_GENERIC;
        uint8_t flags = 0;
        char subtype[14] = {0};
        int8_t ref1m = -59;
        classifyBleDevice(dev, mac, nm, cat, flags, subtype, sizeof(subtype), ref1m);

        radarUpsertTarget(
            s_radarCtx,
            RadarTargetType::BLE,
            cat,
            flags,
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

static int distToRadius(float distM, int maxR, float rangeMaxM = 30.0f) {
    float span = max(5.0f, rangeMaxM);
    float norm = log10f(1.0f + constrain(distM, 0.2f, span)) / log10f(1.0f + span);
    int r = (int)(norm * (float)(maxR - 6)) + 4;
    return constrain(r, 4, maxR - 3);
}

static const char *modeLabel(RadarMode mode) {
    switch (mode) {
        case RadarMode::WIFI_ONLY: return "Filter: WiFi";
        case RadarMode::BLE_ONLY: return "Filter: BLE";
        case RadarMode::ALERTS_ONLY: return "Filter: Alerts";
        case RadarMode::OPEN_WIFI: return "Filter: Open/WEP";
        default: return "Filter: Both";
    }
}

static const char *modeShort(RadarMode mode) {
    switch (mode) {
        case RadarMode::WIFI_ONLY: return "WiFi";
        case RadarMode::BLE_ONLY: return "BLE";
        case RadarMode::ALERTS_ONLY: return "Alrt";
        case RadarMode::OPEN_WIFI: return "Open";
        default: return "All";
    }
}

static const char *viewName(RadarView v, bool hasLocked) {
    switch (v) {
        case RadarView::TABLE: return "Intel Table";
        case RadarView::FOX_SPEC: return hasLocked ? "Fox-Hunt" : "2.4G Spectrum";
        case RadarView::THREAT: return "EW Threat Console";
        default: return "Tactical Radar";
    }
}

static void pruneStaleTargets(RadarContext &ctx) {
    uint32_t now = millis();
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        if (ctx.targets[i].active && (now - ctx.targets[i].lastSeenMs) > RADAR_STALE_MS) {
            bool isLocked = ctx.hasLocked && isSameTarget(ctx.targets[i], ctx.lockedType, ctx.lockedMac);
            if (!isLocked || (now - ctx.targets[i].lastSeenMs) > RADAR_STALE_MS * 2) {
                ctx.targets[i].active = false;
            }
        }
    }
    portEXIT_CRITICAL(&ctx.mux);
}

static std::vector<RadarTarget> snapshotTargets(RadarContext &ctx, RadarMode mode, bool ignoreFilter = false) {
    std::vector<RadarTarget> list;
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        const RadarTarget &t = ctx.targets[i];
        if (!t.active) continue;
        bool isLocked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
        if (!ignoreFilter) {
            if (mode == RadarMode::WIFI_ONLY && t.type != RadarTargetType::WIFI) continue;
            if (mode == RadarMode::BLE_ONLY && t.type != RadarTargetType::BLE) continue;
            if (mode == RadarMode::ALERTS_ONLY && (t.flags & RFLAG_ANY_ALERT) == 0) continue;
            if (mode == RadarMode::OPEN_WIFI &&
                !(t.type == RadarTargetType::WIFI &&
                  (t.category == RadarCategory::WIFI_OPEN || t.category == RadarCategory::WIFI_WEP)))
                continue;
            if (ctx.noiseFilter && !isLocked && isWeakUnnamedBle(t)) continue;
        }
        list.push_back(t);
    }
    portEXIT_CRITICAL(&ctx.mux);
    // Stable priority order: active alerts first, then closest/strongest (quantized to reduce row jitter)
    std::sort(list.begin(), list.end(), [](const RadarTarget &a, const RadarTarget &b) {
        int pa = targetPriorityScore(a);
        int pb = targetPriorityScore(b);
        if (pa != pb) return pa > pb;
        int qa = (int)roundf(a.distM * 2.0f);
        int qb = (int)roundf(b.distM * 2.0f);
        if (qa != qb) return qa < qb;
        if (a.type != b.type) return a.type < b.type;
        return memcmp(a.mac, b.mac, 6) < 0;
    });
    return list;
}

static void setFocusedTarget(RadarContext &ctx, const RadarTarget &t) {
    ctx.hasFocused = true;
    ctx.focusedType = t.type;
    memcpy(ctx.focusedMac, t.mac, 6);
}

static void syncCursorToFocus(RadarContext &ctx, const std::vector<RadarTarget> &list, int &cursor) {
    if (list.empty()) {
        cursor = 0;
        ctx.hasFocused = false;
        return;
    }
    if (ctx.hasFocused) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (isSameTarget(list[i], ctx.focusedType, ctx.focusedMac)) {
                cursor = (int)i;
                return;
            }
        }
    }
    cursor = constrain(cursor, 0, (int)list.size() - 1);
    setFocusedTarget(ctx, list[cursor]);
}

static void lockOnTarget(RadarContext &ctx, const RadarTarget &t) {
    ctx.hasLocked = true;
    ctx.lockedType = t.type;
    memcpy(ctx.lockedMac, t.mac, 6);
    setFocusedTarget(ctx, t);
    ctx.foxHistCount = 0;
    ctx.foxPeakRssi = t.rssi;
}

/*
 * VIEW 0: clean low-noise PPI radar scope renderer.
 * Draws a calm dish with 2 range rings, restrained sweep, real range-filtered
 * blips (3 shapes, 4 semantic colors), and crisp focus/lock reticles.
 */
template <typename Canvas>
static void drawTargetReticle(Canvas &c, int bx, int by, int b, uint16_t col) {
    c.fillRect(bx - b, by - b, 3, 1, col);
    c.drawFastVLine(bx - b, by - b, 3, col);
    c.fillRect(bx + b - 2, by - b, 3, 1, col);
    c.drawFastVLine(bx + b, by - b, 3, col);
    c.fillRect(bx - b, by + b, 3, 1, col);
    c.drawFastVLine(bx - b, by + b - 2, 3, col);
    c.fillRect(bx + b - 2, by + b, 3, 1, col);
    c.drawFastVLine(bx + b, by + b - 2, 3, col);
}

template <typename Canvas>
static void drawRadarScopeCanvas(
    Canvas &c, int cx, int cy, int r, float sweepRad, const std::vector<RadarTarget> &list,
    const RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    const bool isNezha2 = (bruceConfig.nezhaThemeStyle >= 4);
    const uint16_t scopeBg = isNezha2 ? COL_N2_SCOPE_BG : COL_SCOPE_BG;
    const uint16_t gridDim = isNezha2 ? COL_N2_GRID_DIM : COL_GRID_DIM;
    const uint16_t gridMid = isNezha2 ? COL_N2_GRID_MID : COL_GRID_MID;
    const uint16_t sweepF0 = isNezha2 ? COL_N2_SWEEP_F0 : COL_SWEEP_F0;
    const uint16_t sweepF1 = isNezha2 ? COL_N2_SWEEP_F1 : COL_SWEEP_F1;
    const uint16_t sweepLead = isNezha2 ? COL_N2_SWEEP_LEAD : COL_SWEEP_LEAD;

    // 1) Calm circular radar dish
    c.fillRect(cx - r - 3, cy - r - 3, (r + 3) * 2 + 1, (r + 3) * 2 + 1, bg);
    c.fillCircle(cx, cy, r, scopeBg);

    // 2) Subtle narrow 2-step trailing sweep (~16 deg) so blips stay completely legible
    const float bandStep = 0.14f;
    const int sweepR = r - 2;
    {
        float a0 = sweepRad - 2.0f * bandStep;
        float a1 = sweepRad - bandStep;
        float a2 = sweepRad;
        int x0 = cx + (int)(cosf(a0) * sweepR);
        int y0 = cy + (int)(sinf(a0) * sweepR);
        int x1 = cx + (int)(cosf(a1) * sweepR);
        int y1 = cy + (int)(sinf(a1) * sweepR);
        int x2 = cx + (int)(cosf(a2) * sweepR);
        int y2 = cy + (int)(sinf(a2) * sweepR);
        c.fillTriangle(cx, cy, x0, y0, x1, y1, sweepF0);
        c.fillTriangle(cx, cy, x1, y1, x2, y2, sweepF1);
    }

    // 3) Minimal range rings (10m: 2m/5m, 30m: 5m/15m, 60m: 10m/30m) + thin crosshairs
    float innerM = (ctx.rangeMaxM <= 12.0f) ? 2.0f : ((ctx.rangeMaxM <= 35.0f) ? 5.0f : 10.0f);
    float midM = (ctx.rangeMaxM <= 12.0f) ? 5.0f : ((ctx.rangeMaxM <= 35.0f) ? 15.0f : 30.0f);
    int rIn = distToRadius(innerM, r, ctx.rangeMaxM);
    int rMid = distToRadius(midM, r, ctx.rangeMaxM);

    c.fillRect(cx - r + 2, cy, 2 * r - 3, 1, gridDim);
    c.drawFastVLine(cx, cy - r + 2, 2 * r - 3, gridDim);
    c.drawCircle(cx, cy, rIn, gridDim);
    c.drawCircle(cx, cy, rMid, gridDim);

    // 4) Single clean outer bezel + 4 subtle cardinal ticks
    c.drawCircle(cx, cy, r, gridMid);
    c.drawFastVLine(cx, cy - r, 4, gridMid);
    c.drawFastVLine(cx, cy + r - 3, 4, gridMid);
    c.fillRect(cx - r, cy, 4, 1, gridMid);
    c.fillRect(cx + r - 3, cy, 4, 1, gridMid);

    // 5) Restrained leading sweep line
    int leadX = cx + (int)(cosf(sweepRad) * sweepR);
    int leadY = cy + (int)(sinf(sweepRad) * sweepR);
    c.drawLine(cx, cy, leadX, leadY, sweepLead);

    // 6) Subtle range scale labels (only when dish is large enough)
    if (r >= 46) {
        char ringLbl[8];
        c.setTextSize(1);
        c.setTextColor(gridMid);
        snprintf(ringLbl, sizeof(ringLbl), "%.0fm", innerM);
        c.drawString(ringLbl, cx + 3, cy - rIn + 2, 1);
        snprintf(ringLbl, sizeof(ringLbl), "%.0fm", midM);
        c.drawString(ringLbl, cx + 3, cy - rMid + 2, 1);

        char rngBuf[8];
        snprintf(rngBuf, sizeof(rngBuf), "%.0fm", ctx.rangeMaxM);
        c.setTextColor(uiTextMutedColor(), bg);
        c.drawString(rngBuf, cx + r + 2 - (int)strlen(rngBuf) * LW, cy + r - 6, 1);
    }

    // 7) Plot targets: real range filter (distM <= rangeMaxM; alerts & locked/focused always shown)
    const int maxNormalBlips = (r >= 46) ? 24 : 14;
    int normalPlotted = 0;

    int lockedIdx = -1;
    int focusedIdx = -1;
    int topAlertIdx = -1;

    // Pass 1: normal blips (back-to-front so closer/higher-priority targets sit on top)
    for (int i = (int)list.size() - 1; i >= 0; --i) {
        const RadarTarget &t = list[i];
        bool locked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
        bool focused = ctx.hasFocused && isSameTarget(t, ctx.focusedType, ctx.focusedMac);
        bool isAlert = (t.flags & RFLAG_ANY_ALERT) != 0;

        if (locked) {
            lockedIdx = i;
            continue;
        }
        if (focused) {
            focusedIdx = i;
            continue;
        }
        if (isAlert) {
            topAlertIdx = i; // loop runs high->low, so ends up at highest-priority alert
            continue;
        }

        // Respect selected rangeMaxM so zooming to 10m actually removes distant clutter
        if (t.distM > ctx.rangeMaxM) continue;
        if (normalPlotted >= maxNormalBlips) continue;
        ++normalPlotted;

        int distPx = distToRadius(t.distM, r, ctx.rangeMaxM);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);

        if (isWeakUnnamedBle(t)) {
            c.fillCircle(bx, by, 1, COL_WEAK_DOT);
        } else {
            uint16_t col = categoryColor(t.category, t.flags);
            drawCategoryGlyphTo(c, bx, by, t.category, col, t.flags);
        }
    }

    // Pass 2: alert blips on top of normal devices
    for (int i = (int)list.size() - 1; i >= 0; --i) {
        const RadarTarget &t = list[i];
        if ((t.flags & RFLAG_ANY_ALERT) == 0) continue;
        if (i == lockedIdx || i == focusedIdx) continue;
        int distPx = distToRadius(t.distM, r, ctx.rangeMaxM);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);
        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyphTo(c, bx, by, t.category, col, t.flags);
    }

    auto drawPillLabel = [&](int bx, int by, const RadarTarget &t, uint16_t col) {
        if (!ctx.showLabels || r < 46) return;
        String tag = targetDisplayName(t);
        if ((int)tag.length() > 9) tag = tag.substring(0, 9);
        int tw = (int)tag.length() * LW + 4;
        int th = LH + 2;
        int lx = (bx + 8 + tw <= cx + r) ? (bx + 8) : (bx - tw - 7);
        int ly = constrain(by - th / 2, cy - r + 4, cy + r - th - 4);
        lx = constrain(lx, cx - r + 4, cx + r - tw - 4);
        c.fillRect(lx, ly, tw, th, scopeBg);
        c.drawRect(lx, ly, tw, th, col);
        c.setTextSize(1);
        c.setTextColor(col, scopeBg);
        c.drawString(tag, lx + 2, ly + 1, 1);
    };

    // Pass 3: focused target (cursor highlight)
    if (focusedIdx >= 0 && focusedIdx != lockedIdx) {
        const RadarTarget &t = list[focusedIdx];
        int distPx = distToRadius(t.distM, r, ctx.rangeMaxM);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);
        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyphTo(c, bx, by, t.category, col, t.flags);
        drawTargetReticle(c, bx, by, 5, TFT_WHITE);
        if (lockedIdx < 0) drawPillLabel(bx, by, t, TFT_WHITE);
    }

    // Pass 4: locked target (highest visual prominence + bearing vector line)
    if (lockedIdx >= 0) {
        const RadarTarget &t = list[lockedIdx];
        int distPx = distToRadius(t.distM, r, ctx.rangeMaxM);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);
        c.drawLine(cx, cy, bx, by, blendColors(scopeBg, COL_QIANKUN_GOLD, 120));
        drawTargetReticle(c, bx, by, 6, COL_QIANKUN_GOLD);
        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyphTo(c, bx, by, t.category, col, t.flags);
        c.drawPixel(bx, by, TFT_WHITE);
        drawPillLabel(bx, by, t, COL_QIANKUN_GOLD);
    } else if (topAlertIdx >= 0 && focusedIdx < 0) {
        const RadarTarget &t = list[topAlertIdx];
        int distPx = distToRadius(t.distM, r, ctx.rangeMaxM);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);
        drawPillLabel(bx, by, t, categoryColor(t.category, t.flags));
    }

    // 8) Clean minimal center self-marker (2px dot so close targets are never obscured)
    c.fillCircle(cx, cy, 2, bruceConfig.priColor);
    c.drawPixel(cx, cy, TFT_WHITE);
}

#if !defined(BRUCE_COMPACT_CJK_FONT)
static void drawRadarScope(
    tft_sprite *spr, int cx, int cy, int r, float sweepRad, const std::vector<RadarTarget> &list,
    const RadarContext &ctx
) {
    if (spr) {
        int half = r + 3;
        drawRadarScopeCanvas(*spr, half, half, r, sweepRad, list, ctx);
        spr->pushSprite(cx - half, cy - half);
        return;
    }
    drawRadarScopeCanvas(tft, cx, cy, r, sweepRad, list, ctx);
}
#else
static void drawRadarScope(
    void *, int cx, int cy, int r, float sweepRad, const std::vector<RadarTarget> &list,
    const RadarContext &ctx
) {
    drawRadarScopeCanvas(tft, cx, cy, r, sweepRad, list, ctx);
}
#endif

// Layout metrics for HUD hit-testing (shared between drawRadarHud and touch handler)
struct ScopeHudMetrics {
    int cardY = 0;
    int cardH = 0;
    int listY = 0;
    int rowH = 12;
    int visRows = 0;
};

static ScopeHudMetrics drawRadarHud(
    int hudX, int hudY, int hudW, int hudH, RadarMode mode, bool scanningWifi, bool scanningBle,
    const std::vector<RadarTarget> &list, RadarContext &ctx
) {
    ScopeHudMetrics m;
    const uint16_t bg = bruceConfig.bgColor;
    const uint16_t muted = uiTextMutedColor();
    const uint16_t bright = uiTextBrightColor();
    tft.fillRect(hudX, hudY, hudW, hudH, bg);
    tft.setTextSize(1);

    int wifiCnt = 0, bleCnt = 0, alertCnt = 0;
    const RadarTarget *lockedTarget = nullptr;
    for (const auto &t : list) {
        if (t.type == RadarTargetType::WIFI) ++wifiCnt;
        else ++bleCnt;
        if (t.flags & RFLAG_ANY_ALERT) ++alertCnt;
        if (ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac)) lockedTarget = &t;
    }

    syncCursorToFocus(ctx, list, ctx.scopeCursor);
    const RadarTarget *activeTarget =
        lockedTarget ? lockedTarget : (!list.empty() ? &list[ctx.scopeCursor] : nullptr);

    int y = hudY;

    // 1) Clean compact status header: live scan dot + Filter/Range + W/B counts + Alert badge
    tft.fillRect(hudX, y, hudW, LH + 4, COL_HEADER_BG);
    uint16_t scanDotCol = scanningWifi ? TFT_CYAN : (scanningBle ? COL_BLE_NORM : muted);
    tft.fillCircle(hudX + 4, y + (LH + 4) / 2, 2, scanDotCol);
    char leftHdr[18];
    snprintf(leftHdr, sizeof(leftHdr), "%s %.0fm", modeShort(mode), ctx.rangeMaxM);
    tft.setTextColor(bright, COL_HEADER_BG);
    tft.drawString(leftHdr, hudX + 9, y + 2, 1);

    int rightEdge = hudX + hudW - 2;
    if (alertCnt > 0) {
        char alBuf[8];
        snprintf(alBuf, sizeof(alBuf), "!%d", alertCnt);
        int alW = (int)strlen(alBuf) * LW + 6;
        tft.fillRoundRect(rightEdge - alW, y + 1, alW, LH + 2, 2, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawCentreString(alBuf, rightEdge - alW / 2, y + 2, 1);
        rightEdge -= alW + 3;
    }
    char cntBuf[16];
    snprintf(cntBuf, sizeof(cntBuf), "%dW %dB", wifiCnt, bleCnt);
    if (rightEdge - (int)strlen(cntBuf) * LW > hudX + 9 + (int)strlen(leftHdr) * LW + 3) {
        tft.setTextColor(muted, COL_HEADER_BG);
        tft.drawRightString(cntBuf, rightEdge, y + 2, 1);
    }
    y += LH + 6;

    // 2) Compact inline legend (only on taller screens so the 3 shapes + 4 colors are self-explanatory)
    if (hudH >= 124 && hudW >= 120) {
        int lx = hudX + 1;
        tft.fillRect(lx, y + 2, 4, 4, TFT_CYAN);
        tft.setTextColor(muted, bg);
        tft.drawString("W", lx + 6, y, 1);
        lx += 16;

        tft.fillRect(lx, y + 2, 4, 4, COL_TAC_GREEN);
        tft.drawString("Opn", lx + 6, y, 1);
        lx += 28;

        tft.fillCircle(lx + 2, y + 4, 2, COL_BLE_NORM);
        tft.drawString("BLE", lx + 6, y, 1);
        lx += 28;

        tft.fillTriangle(lx + 2, y + 1, lx, y + 4, lx + 4, y + 4, TFT_RED);
        tft.fillTriangle(lx + 2, y + 7, lx, y + 4, lx + 4, y + 4, TFT_RED);
        tft.drawString("Alrt", lx + 6, y, 1);
        y += LH + 3;
    }

    // 3) Focused / Locked Target Detail Card (actionable intelligence at a glance)
    const bool compactCard = (hudH < 105);
    m.cardY = y;
    m.cardH = compactCard ? (LH * 2 + 11) : (LH * 3 + 17);
    if (y + m.cardH <= hudY + hudH) {
        tft.fillRect(hudX, y, hudW, m.cardH, COL_PANEL_BG);
        tft.drawRect(hudX, y, hudW, m.cardH, uiBorderDimColor());

        if (activeTarget) {
            bool isLockedCard = (lockedTarget != nullptr);
            uint16_t accent =
                isLockedCard ? COL_QIANKUN_GOLD : categoryColor(activeTarget->category, activeTarget->flags);
            tft.fillRect(hudX, y, 2, m.cardH, accent);

            int cx = hudX + 5;
            int cw = hudW - 8;
            int cy = y + 3;

            // Right pill: LOCK or alert tag or channel/radio
            const char *alTag = alertBadgeShort(activeTarget->flags);
            String pill;
            uint16_t pillCol = accent;
            if (isLockedCard) {
                pill = "LOCK";
                pillCol = COL_QIANKUN_GOLD;
            } else if (alTag) {
                pill = alTag;
                pillCol = categoryColor(activeTarget->category, activeTarget->flags);
            } else if (activeTarget->type == RadarTargetType::WIFI) {
                pill = "C" + String(activeTarget->channel);
                pillCol = (activeTarget->category == RadarCategory::WIFI_OPEN) ? COL_TAC_GREEN : TFT_CYAN;
            } else {
                pill = "BLE";
                pillCol = COL_BLE_NORM;
            }

            int pillW = (int)pill.length() * LW + 4;
            tft.fillRect(hudX + hudW - pillW - 2, cy - 1, pillW, LH + 2, pillCol);
            tft.setTextColor(uiTextOnAccentColor(pillCol), pillCol);
            tft.drawCentreString(pill, hudX + hudW - pillW / 2 - 2, cy, 1);

            int nameMaxChars = max(4, (cw - pillW - 4) / LW);
            String nm = targetDisplayName(*activeTarget);
            if ((int)nm.length() > nameMaxChars) nm = nm.substring(0, nameMaxChars);
            tft.setTextColor(bright, COL_PANEL_BG);
            tft.drawString(nm, cx, cy, 1);
            cy += LH + 3;

            // Line 2: RSSI + Distance + Trend
            int pct = constrain((activeTarget->rssi + 95) * 100 / 60, 0, 100);
            uint16_t sigCol = (pct > 66) ? COL_TAC_GREEN : ((pct > 33) ? COL_QIANKUN_GOLD : TFT_RED);
            char trend = (activeTarget->rssi > activeTarget->prevRssi + 1)
                             ? '+'
                             : ((activeTarget->rssi < activeTarget->prevRssi - 1) ? '-' : ' ');
            char sigLine[26];
            if (trend != ' ') {
                snprintf(
                    sigLine,
                    sizeof(sigLine),
                    "%ddBm %s %c",
                    (int)activeTarget->rssi,
                    formatDistShort(activeTarget->distM).c_str(),
                    trend
                );
            } else {
                snprintf(
                    sigLine,
                    sizeof(sigLine),
                    "%ddBm  %s",
                    (int)activeTarget->rssi,
                    formatDistShort(activeTarget->distM).c_str()
                );
            }
            tft.setTextColor(sigCol, COL_PANEL_BG);
            tft.drawString(String(sigLine).substring(0, max(6, cw / LW)), cx, cy, 1);
            cy += LH + 2;

            // Line 3 (full card): Vendor / Auth / Subtype / Triangulated bearing
            if (!compactCard) {
                char infoBuf[32];
                const char *oui = lookupOuiVendor(activeTarget->mac);
                if (activeTarget->hasTriangulated) {
                    float brg = vectorToBearingDeg(activeTarget->relX, activeTarget->relY);
                    snprintf(
                        infoBuf,
                        sizeof(infoBuf),
                        "Brg %03d %s (%+.0f,%+.0f)",
                        (int)roundf(brg),
                        bearingToCardinal(brg),
                        activeTarget->relX,
                        activeTarget->relY
                    );
                } else if (activeTarget->type == RadarTargetType::WIFI) {
                    if (oui) {
                        snprintf(
                            infoBuf,
                            sizeof(infoBuf),
                            "%s | %s",
                            wifiAuthShort(activeTarget->authMode),
                            oui
                        );
                    } else {
                        snprintf(
                            infoBuf,
                            sizeof(infoBuf),
                            "%s | %02X:%02X:%02X",
                            wifiAuthShort(activeTarget->authMode),
                            activeTarget->mac[3],
                            activeTarget->mac[4],
                            activeTarget->mac[5]
                        );
                    }
                } else {
                    const char *sub =
                        activeTarget->subtype[0] ? activeTarget->subtype : (oui ? oui : "BLE");
                    snprintf(
                        infoBuf,
                        sizeof(infoBuf),
                        "%s | %s",
                        sub,
                        formatDwell(activeTarget->firstSeenMs, millis()).c_str()
                    );
                }
                tft.setTextColor(muted, COL_PANEL_BG);
                tft.drawString(String(infoBuf).substring(0, max(6, cw / LW)), cx, cy, 1);
                cy += LH + 3;
            }

            // Sleek signal strength bar
            int barH = compactCard ? 3 : 4;
            if (cy + barH <= y + m.cardH - 2) {
                tft.fillRect(cx, cy, cw, barH, bg);
                int fillW = (cw * pct) / 100;
                if (fillW > 0) tft.fillRect(cx, cy, fillW, barH, sigCol);
            }
        } else if (ctx.hasLocked) {
            tft.fillRect(hudX, y, 2, m.cardH, TFT_RED);
            tft.setTextColor(TFT_RED, COL_PANEL_BG);
            tft.drawString(bruce_i18n::tr("Lost signal..."), hudX + 6, y + 4, 1);
            tft.setTextColor(muted, COL_PANEL_BG);
            tft.drawString(formatMac(ctx.lockedMac).substring(9), hudX + 6, y + LH + 7, 1);
        } else {
            tft.setTextColor(muted, COL_PANEL_BG);
            tft.drawCentreString(bruce_i18n::tr("Scanning..."), hudX + hudW / 2, y + m.cardH / 2 - 4, 1);
        }
        y += m.cardH + 3;
    }

    // 4) Interactive Scrollable Nearby List (UP/DOWN or tap to focus/lock)
    m.listY = y;
    m.rowH = (hudH >= 120) ? (LH + 4) : (LH + 2);
    m.visRows = max(0, (hudY + hudH - y) / m.rowH);
    if (m.visRows <= 0 || list.empty()) return m;

    if (ctx.scopeCursor < ctx.scopeScroll) ctx.scopeScroll = ctx.scopeCursor;
    if (ctx.scopeCursor >= ctx.scopeScroll + m.visRows) ctx.scopeScroll = ctx.scopeCursor - m.visRows + 1;
    ctx.scopeScroll = constrain(ctx.scopeScroll, 0, max(0, (int)list.size() - m.visRows));

    const int maxChars = max(6, (hudW - 10) / LW);
    for (int r = 0; r < m.visRows && (ctx.scopeScroll + r) < (int)list.size(); ++r) {
        int idx = ctx.scopeScroll + r;
        const RadarTarget &t = list[idx];
        bool isSel = (idx == ctx.scopeCursor);
        bool isLocked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
        bool outOfRange = (t.distM > ctx.rangeMaxM) && !(t.flags & RFLAG_ANY_ALERT) && !isLocked;

        uint16_t rowBg = isSel ? COL_ROW_SEL : bg;
        if (isSel) {
            tft.fillRect(hudX, y, hudW, m.rowH - 1, rowBg);
            tft.fillRect(hudX, y, 2, m.rowH - 1, isLocked ? COL_QIANKUN_GOLD : bruceConfig.priColor);
        }

        uint16_t col = outOfRange ? muted : categoryColor(t.category, t.flags);
        drawCategoryGlyph(hudX + 5, y + (m.rowH - 1) / 2, t.category, col, t.flags);

        String distStr = formatDistShort(t.distM);
        int nameBudget = max(3, maxChars - (int)distStr.length() - 2);
        String nm = (isLocked ? "*" : "") + targetDisplayName(t);
        if ((int)nm.length() > nameBudget) nm = nm.substring(0, nameBudget);

        tft.setTextColor(isLocked ? COL_QIANKUN_GOLD : (isSel ? bright : col), rowBg);
        tft.drawString(nm, hudX + 11, y + 1, 1);
        tft.setTextColor(outOfRange ? muted : (isSel ? bright : muted), rowBg);
        tft.drawRightString(distStr, hudX + hudW - 2, y + 1, 1);
        y += m.rowH;
    }
    return m;
}

/*
 * VIEW 1: clean, scannable device list with bottom detail strip for the selected row.
 */
struct TableViewMetrics {
    int rowsTop = 0;
    int rowH = 13;
    int visRows = 0;
};

static TableViewMetrics drawIntelTable(
    int x, int y, int w, int h, RadarMode mode, bool scanningWifi, bool scanningBle,
    const std::vector<RadarTarget> &list, RadarContext &ctx
) {
    TableViewMetrics tm;
    const uint16_t bg = bruceConfig.bgColor;
    const uint16_t muted = uiTextMutedColor();
    const uint16_t bright = uiTextBrightColor();
    tft.fillRect(x, y, w, h, bg);
    tft.setTextSize(1);

    int total = (int)list.size();
    syncCursorToFocus(ctx, list, ctx.tableCursor);

    int wifiCnt = 0, bleCnt = 0, alertCnt = 0;
    for (const auto &t : list) {
        if (t.type == RadarTargetType::WIFI) ++wifiCnt;
        else ++bleCnt;
        if (t.flags & RFLAG_ANY_ALERT) ++alertCnt;
    }

    // 1) Clean summary bar
    tft.fillRect(x, y, w, LH + 4, COL_HEADER_BG);
    uint16_t scanCol = scanningWifi ? TFT_CYAN : (scanningBle ? COL_BLE_NORM : muted);
    tft.fillCircle(x + 5, y + (LH + 4) / 2, 2, scanCol);
    char topInfo[42];
    snprintf(topInfo, sizeof(topInfo), "%s  WiFi:%d  BLE:%d", modeShort(mode), wifiCnt, bleCnt);
    tft.setTextColor(bright, COL_HEADER_BG);
    tft.drawString(topInfo, x + 11, y + 2, 1);

    if (alertCnt > 0) {
        char alBuf[10];
        snprintf(alBuf, sizeof(alBuf), "!%d ALRT", alertCnt);
        int alW = (int)strlen(alBuf) * LW + 6;
        tft.fillRoundRect(x + w - alW - 2, y + 1, alW, LH + 2, 2, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawCentreString(alBuf, x + w - alW / 2 - 2, y + 2, 1);
    } else if (ctx.noiseFilter) {
        tft.setTextColor(muted, COL_HEADER_BG);
        tft.drawRightString("Clean", x + w - 3, y + 2, 1);
    }
    int curY = y + LH + 5;

    // 2) Clean non-overlapping column headers
    tft.fillRect(x, curY, w, LH + 3, COL_PANEL_BG);
    tft.setTextColor(muted, COL_PANEL_BG);
    const int colName = x + 11;
    const bool showTagCol = (w >= 210);
    const int colDistR = x + w - 3;
    const int colRssiR = colDistR - 36;
    const int colTag = showTagCol ? (x + (w * 46) / 100) : colRssiR;

    tft.drawString("Device", colName, curY + 1, 1);
    if (showTagCol) tft.drawString("Info", colTag, curY + 1, 1);
    tft.drawRightString("dBm", colRssiR, curY + 1, 1);
    tft.drawRightString("Dist", colDistR, curY + 1, 1);
    curY += LH + 4;

    if (total == 0) {
        tft.setTextColor(muted, bg);
        tft.drawCentreString(bruce_i18n::tr("Scanning nearby BLE & WiFi..."), x + w / 2, curY + 20, 1);
        return tm;
    }

    // Reserve bottom detail strip on taller screens so selected row's full MAC/vendor/alert is visible
    const bool showDetailBar = (h >= 105);
    const int detailH = showDetailBar ? (LH * 2 + 7) : 0;
    const int rowsBot = y + h - detailH - (showDetailBar ? 2 : 0);

    tm.rowsTop = curY;
    tm.rowH = (h >= 120) ? (LH + 5) : (LH + 3);
    tm.visRows = max(1, (rowsBot - curY) / tm.rowH);
    if (ctx.tableCursor < ctx.tableScroll) ctx.tableScroll = ctx.tableCursor;
    if (ctx.tableCursor >= ctx.tableScroll + tm.visRows) ctx.tableScroll = ctx.tableCursor - tm.visRows + 1;
    ctx.tableScroll = constrain(ctx.tableScroll, 0, max(0, total - tm.visRows));

    int nameMaxPx = (showTagCol ? colTag : (colRssiR - 26)) - colName - 4;
    int nameChars = max(5, nameMaxPx / LW);
    int tagMaxPx = (colRssiR - 26) - colTag - 4;
    int tagChars = max(4, tagMaxPx / LW);

    for (int r = 0; r < tm.visRows && (ctx.tableScroll + r) < total; ++r) {
        int idx = ctx.tableScroll + r;
        const RadarTarget &t = list[idx];
        bool sel = (idx == ctx.tableCursor);
        bool locked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
        uint16_t rowBg = sel ? COL_ROW_SEL : bg;
        if (sel) {
            tft.fillRect(x, curY, w, tm.rowH - 1, rowBg);
            tft.fillRect(x, curY, 2, tm.rowH - 1, locked ? COL_QIANKUN_GOLD : bruceConfig.priColor);
        }

        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyph(x + 5, curY + (tm.rowH - 1) / 2, t.category, col, t.flags);

        String nm = (locked ? "*" : "") + targetDisplayName(t);
        if ((int)nm.length() > nameChars) nm = nm.substring(0, nameChars);
        tft.setTextColor(locked ? COL_QIANKUN_GOLD : (sel ? bright : col), rowBg);
        tft.drawString(nm, colName, curY + 2, 1);

        if (showTagCol) {
            String tg = targetShortTag(t);
            if ((int)tg.length() > tagChars) tg = tg.substring(0, tagChars);
            tft.setTextColor((t.flags & RFLAG_ANY_ALERT) ? TFT_RED : muted, rowBg);
            tft.drawString(tg, colTag, curY + 2, 1);
        }

        int pct = constrain((t.rssi + 95) * 100 / 60, 0, 100);
        uint16_t rssiCol = (pct > 66) ? COL_TAC_GREEN : ((pct > 33) ? COL_QIANKUN_GOLD : TFT_RED);
        tft.setTextColor(rssiCol, rowBg);
        tft.drawRightString(String(t.rssi), colRssiR, curY + 2, 1);

        tft.setTextColor(sel ? bright : muted, rowBg);
        tft.drawRightString(formatDistShort(t.distM), colDistR, curY + 2, 1);

        curY += tm.rowH;
    }

    // 3) Selected-row detail footer (full MAC, OUI vendor, dwell, and alert reason)
    if (showDetailBar && ctx.tableCursor >= 0 && ctx.tableCursor < total) {
        const RadarTarget &selT = list[ctx.tableCursor];
        int dy = y + h - detailH;
        tft.fillRect(x, dy, w, detailH, COL_PANEL_BG);
        tft.drawFastHLine(x, dy, w, uiBorderDimColor());

        const char *oui = lookupOuiVendor(selT.mac);
        char line1[48];
        if (oui) {
            snprintf(line1, sizeof(line1), "%s  [%s]", formatMac(selT.mac).c_str(), oui);
        } else {
            snprintf(line1, sizeof(line1), "%s", formatMac(selT.mac).c_str());
        }
        tft.setTextColor(bright, COL_PANEL_BG);
        tft.drawString(String(line1).substring(0, max(10, (w - 46) / LW)), x + 4, dy + 3, 1);
        tft.setTextColor(muted, COL_PANEL_BG);
        tft.drawRightString(formatDwell(selT.firstSeenMs, millis()), x + w - 4, dy + 3, 1);

        const char *reason = alertReasonText(selT.flags);
        if (reason && reason[0]) {
            tft.setTextColor((selT.flags & RFLAG_ANY_ALERT) ? TFT_RED : COL_TAC_GREEN, COL_PANEL_BG);
            tft.drawString(String(reason).substring(0, max(10, (w - 8) / LW)), x + 4, dy + LH + 5, 1);
        } else {
            char line2[48];
            snprintf(
                line2,
                sizeof(line2),
                "%s  Peak:%ddBm  Pkts:%u",
                categoryName(selT.category),
                (int)selT.peakRssi,
                (unsigned)selT.seenCount
            );
            tft.setTextColor(muted, COL_PANEL_BG);
            tft.drawString(String(line2).substring(0, max(10, (w - 8) / LW)), x + 4, dy + LH + 5, 1);
        }
    }
    return tm;
}

/*
 * VIEW 2: fox-hunt or 2.4 GHz channel spectrum display, selected by lock state.
 */
static void drawFoxHuntOrSpectrum(
    int x, int y, int w, int h, const std::vector<RadarTarget> &allList, RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    const uint16_t muted = uiTextMutedColor();
    const uint16_t bright = uiTextBrightColor();
    tft.fillRect(x, y, w, h, bg);

    if (ctx.hasLocked) {
        const RadarTarget *lt = nullptr;
        for (const auto &t : allList) {
            if (isSameTarget(t, ctx.lockedType, ctx.lockedMac)) {
                lt = &t;
                break;
            }
        }

        // Clean header strip
        tft.fillRect(x, y, w, LH + 4, COL_HEADER_BG);
        tft.setTextSize(1);
        String title = lt ? (targetDisplayName(*lt) + " [" + targetShortTag(*lt) + "]")
                          : (formatMac(ctx.lockedMac) + " (LOST)");
        int beepW = 46;
        tft.setTextColor(COL_QIANKUN_GOLD, COL_HEADER_BG);
        tft.drawString(title.substring(0, max(8, (w - beepW - 8) / LW)), x + 4, y + 2, 1);

        uint16_t beepBg = ctx.geigerAudio ? COL_TAC_GREEN : COL_PANEL_BG;
        tft.fillRoundRect(x + w - beepW - 2, y + 1, beepW, LH + 2, 2, beepBg);
        tft.setTextColor(ctx.geigerAudio ? uiTextOnAccentColor(beepBg) : muted, beepBg);
        tft.drawCentreString(ctx.geigerAudio ? "BEEP" : "MUTE", x + w - beepW / 2 - 2, y + 2, 1);

        int curY = y + LH + 7;
        if (lt) {
            int pct = constrain((lt->rssi + 95) * 100 / 60, 0, 100);
            uint16_t sigCol = (pct > 66) ? COL_TAC_GREEN : ((pct > 33) ? COL_QIANKUN_GOLD : TFT_RED);

            // Big RSSI + clear Trend badge + Distance
            tft.setTextSize(FM);
            tft.setTextColor(sigCol, bg);
            tft.drawString(String(lt->rssi) + " dBm", x + 2, curY, 1);

            int slope = 0;
            if (ctx.foxHistCount >= 6) {
                int recent = (ctx.foxHist[ctx.foxHistCount - 1] + ctx.foxHist[ctx.foxHistCount - 2] +
                              ctx.foxHist[ctx.foxHistCount - 3]) /
                             3;
                int older = (ctx.foxHist[ctx.foxHistCount - 4] + ctx.foxHist[ctx.foxHistCount - 5] +
                             ctx.foxHist[ctx.foxHistCount - 6]) /
                            3;
                slope = recent - older;
            }
            const char *trendLbl = (slope >= 2) ? "CLOSER" : ((slope <= -2) ? "FARTHER" : "STEADY");
            uint16_t trendCol = (slope >= 2) ? COL_TAC_GREEN : ((slope <= -2) ? TFT_RED : COL_QIANKUN_GOLD);
            if (w >= 220) {
                tft.setTextSize(1);
                tft.fillRoundRect(x + w / 2 - 26, curY + 2, 54, LH + 4, 3, trendCol);
                tft.setTextColor(uiTextOnAccentColor(trendCol), trendCol);
                tft.drawCentreString(trendLbl, x + w / 2 + 1, curY + 4, 1);
            }

            tft.setTextSize(FM);
            tft.setTextColor(bright, bg);
            tft.drawRightString(formatDistShort(lt->distM), x + w - 2, curY, 1);
            curY += FM * LH + 4;

            // Clean metadata line
            tft.setTextSize(1);
            char meta[56];
            snprintf(
                meta,
                sizeof(meta),
                "Peak:%ddBm  Pkts:%u  %s",
                (int)ctx.foxPeakRssi,
                (unsigned)lt->seenCount,
                formatDwell(lt->firstSeenMs, millis()).c_str()
            );
            tft.setTextColor(muted, bg);
            tft.drawString(String(meta).substring(0, max(10, w / LW)), x + 2, curY, 1);
            curY += LH + 4;

            // Clean segmented proximity bar
            int barH = 9;
            tft.fillRect(x + 2, curY, w - 4, barH, COL_PANEL_BG);
            const int segs = 16;
            const int sGap = 2;
            int segW = max(3, (w - 8 - sGap * (segs - 1)) / segs);
            int lit = (pct * segs + 50) / 100;
            for (int s = 0; s < segs; ++s) {
                uint16_t sc = (s < lit) ? ((s >= 11) ? COL_TAC_GREEN : ((s >= 6) ? COL_QIANKUN_GOLD : TFT_RED))
                                        : COL_GRID_DIM;
                tft.fillRect(x + 4 + s * (segW + sGap), curY + 2, segW, barH - 4, sc);
            }
            curY += barH + 5;
        } else {
            tft.setTextSize(FM);
            tft.setTextColor(TFT_RED, bg);
            tft.drawCentreString(bruce_i18n::tr("Lost signal..."), x + w / 2, curY + 6, 1);
            curY += FM * LH + 18;
        }

        // Rolling RSSI Sparkline with dedicated left axis margin (labels never overlap trace)
        int axisW = 24;
        int gx = x + axisW;
        int gy = curY;
        int gw = w - axisW - 2;
        int gh = (y + h) - gy - 2;
        if (gh >= 24 && gw >= 40) {
            tft.fillRect(gx, gy, gw, gh, COL_PANEL_BG);
            tft.drawRect(gx, gy, gw, gh, uiBorderDimColor());
            tft.drawFastHLine(gx + 1, gy + gh / 2, gw - 2, COL_GRID_DIM);

            tft.setTextSize(1);
            tft.setTextColor(muted, bg);
            tft.drawRightString("-35", gx - 2, gy, 1);
            tft.drawRightString("-95", gx - 2, gy + gh - LH, 1);

            if (ctx.foxPeakRssi > -98) {
                int pkClamp = constrain((int)ctx.foxPeakRssi, -98, -32);
                int pkY = gy + gh - 2 - ((pkClamp + 98) * (gh - 4)) / 66;
                for (int dx = gx + 2; dx < gx + gw - 4; dx += 6) {
                    tft.drawFastHLine(dx, pkY, 3, COL_QIANKUN_GOLD);
                }
            }

            if (ctx.foxHistCount >= 2) {
                int prevPx = -1, prevPy = -1;
                for (size_t i = 0; i < ctx.foxHistCount; ++i) {
                    int px = gx + 2 + (int)((i * (gw - 4)) / (RADAR_HIST_LEN - 1));
                    int clamped = constrain((int)ctx.foxHist[i], -98, -32);
                    int py = gy + gh - 2 - ((clamped + 98) * (gh - 4)) / 66;
                    uint16_t ptCol = (clamped > -58) ? COL_TAC_GREEN
                                                     : ((clamped > -75) ? COL_QIANKUN_GOLD : TFT_RED);
                    if (prevPx >= 0) tft.drawLine(prevPx, prevPy, px, py, ptCol);
                    prevPx = px;
                    prevPy = py;
                }
            }
        }
        return;
    }

    /*
     * Unlocked spectrum branch: clean 2.4 GHz channel congestion chart (ch 1..13)
     */
    int chCount[14] = {0};
    int chPeak[14];
    for (int c = 0; c < 14; ++c) chPeak[c] = -100;

    int openCnt = 0, hiddenCnt = 0, alertCnt = 0;
    for (const auto &t : allList) {
        if (t.type == RadarTargetType::WIFI) {
            if (t.channel >= 1 && t.channel <= 13) {
                chCount[t.channel]++;
                if (t.rssi > chPeak[t.channel]) chPeak[t.channel] = t.rssi;
            }
            if (t.category == RadarCategory::WIFI_OPEN || t.category == RadarCategory::WIFI_WEP) ++openCnt;
            if (t.category == RadarCategory::WIFI_HIDDEN) ++hiddenCnt;
        }
        if (t.flags & RFLAG_ANY_ALERT) ++alertCnt;
    }

    // Recommend cleanest standard channel (prefer 1, 6, 11 on tie)
    int bestCh = 1, busyCh = 1;
    for (int c : {1, 6, 11}) {
        if (chCount[c] < chCount[bestCh]) bestCh = c;
    }
    for (int c = 1; c <= 13; ++c) {
        if (chCount[c] < chCount[bestCh]) bestCh = c;
        if (chCount[c] > chCount[busyCh]) busyCh = c;
    }

    // Clean top bar: Best Channel + Busiest Channel
    tft.fillRect(x, y, w, LH + 4, COL_HEADER_BG);
    tft.setTextSize(1);
    char hdr[48];
    if (chCount[busyCh] > 0) {
        snprintf(hdr, sizeof(hdr), "Best: Ch%d   Busy: Ch%d (%d)", bestCh, busyCh, chCount[busyCh]);
    } else {
        snprintf(hdr, sizeof(hdr), "2.4GHz Channels (scanning...)");
    }
    tft.setTextColor(bright, COL_HEADER_BG);
    tft.drawString(String(hdr).substring(0, max(10, (w - 42) / LW)), x + 4, y + 2, 1);

    if (alertCnt > 0) {
        char alBuf[8];
        snprintf(alBuf, sizeof(alBuf), "!%d", alertCnt);
        int alW = (int)strlen(alBuf) * LW + 6;
        tft.fillRoundRect(x + w - alW - 2, y + 1, alW, LH + 2, 2, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawCentreString(alBuf, x + w - alW / 2 - 2, y + 2, 1);
    }

    // Channel bar chart (1..13)
    const bool showFooterHint = (h >= 115);
    int chartTop = y + LH + 12;
    int chartBot = y + h - LH - (showFooterHint ? 16 : 4);
    int chartH = max(20, chartBot - chartTop);
    tft.drawFastHLine(x + 2, chartBot, w - 4, uiBorderDimColor());
    tft.drawFastHLine(x + 2, chartTop + chartH / 2, w - 4, COL_GRID_DIM);

    int maxCount = 1;
    for (int c = 1; c <= 13; ++c) {
        if (chCount[c] > maxCount) maxCount = chCount[c];
    }

    int slotW = (w - 8) / 13;
    int barW = max(4, slotW - 3);
    for (int c = 1; c <= 13; ++c) {
        int bx = x + 4 + (c - 1) * slotW;
        if (chCount[c] > 0) {
            int bh = max(4, (chCount[c] * (chartH - 10)) / maxCount);
            uint16_t bCol = (chCount[c] >= 4 || chPeak[c] > -55)
                                ? TFT_RED
                                : ((chCount[c] >= 2) ? COL_QIANKUN_GOLD : COL_TAC_GREEN);
            tft.fillRect(bx, chartBot - bh, barW, bh, bCol);
            tft.setTextColor(bright, bg);
            tft.drawCentreString(String(chCount[c]), bx + barW / 2, chartBot - bh - LH - 1, 1);
        }
        bool stdCh = (c == 1 || c == 6 || c == 11);
        tft.setTextColor(stdCh ? COL_QIANKUN_GOLD : muted, bg);
        tft.drawCentreString(String(c), bx + barW / 2, chartBot + 2, 1);
    }

    if (showFooterHint) {
        char foot[56];
        snprintf(foot, sizeof(foot), "Open:%d  Hidden:%d  |  Lock target for Fox-Hunt", openCnt, hiddenCnt);
        tft.setTextColor(muted, bg);
        tft.drawString(String(foot).substring(0, max(10, (w - 6) / LW)), x + 3, y + h - LH - 2, 1);
    }
}

/*
 * VIEW 3: actionable Alerts & Environment summary (no fake DEFCON / system-log noise).
 */
struct AlertViewMetrics {
    int rowsTop = 0;
    int rowH = 13;
    int visRows = 0;
};

static std::vector<RadarTarget> extractAlertTargets(const std::vector<RadarTarget> &allList) {
    std::vector<RadarTarget> alerts;
    for (const auto &t : allList) {
        if (t.flags & RFLAG_ANY_ALERT) alerts.push_back(t);
    }
    return alerts;
}

static AlertViewMetrics drawThreatConsole(
    int x, int y, int w, int h, const std::vector<RadarTarget> &allList, RadarContext &ctx
) {
    AlertViewMetrics am;
    const uint16_t bg = bruceConfig.bgColor;
    const uint16_t muted = uiTextMutedColor();
    const uint16_t bright = uiTextBrightColor();
    tft.fillRect(x, y, w, h, bg);
    tft.setTextSize(1);

    int wifiCnt = 0, bleCnt = 0, openCnt = 0, hiddenCnt = 0;
    int twins = 0, trackers = 0, stalkers = 0, drones = 0, hacks = 0;
    const char *topVendor = nullptr;
    int topVendorCnt = 0;

    for (size_t i = 0; i < allList.size(); ++i) {
        const auto &t = allList[i];
        if (t.type == RadarTargetType::WIFI) {
            ++wifiCnt;
            if (t.category == RadarCategory::WIFI_OPEN || t.category == RadarCategory::WIFI_WEP) ++openCnt;
            if (t.category == RadarCategory::WIFI_HIDDEN) ++hiddenCnt;
        } else {
            ++bleCnt;
        }
        if (t.flags & RFLAG_EVIL_TWIN) ++twins;
        if (t.flags & RFLAG_TRACKER) ++trackers;
        if (t.flags & RFLAG_STALKER) ++stalkers;
        if (t.flags & RFLAG_DRONE) ++drones;
        if (t.flags & RFLAG_HACK_DEV) ++hacks;

        const char *v = lookupOuiVendor(t.mac);
        if (v) {
            int cnt = 0;
            for (size_t j = i; j < allList.size(); ++j) {
                const char *v2 = lookupOuiVendor(allList[j].mac);
                if (v2 && strcmp(v, v2) == 0) ++cnt;
            }
            if (cnt > topVendorCnt) {
                topVendorCnt = cnt;
                topVendor = v;
            }
        }
    }

    std::vector<RadarTarget> alerts = extractAlertTargets(allList);
    int curY = y;

    // 1) Clean honest status header
    tft.fillRect(x, curY, w, LH + 6, COL_HEADER_BG);
    if (alerts.empty()) {
        tft.fillRoundRect(x + 3, curY + 1, 64, LH + 4, 2, COL_TAC_GREEN);
        tft.setTextColor(uiTextOnAccentColor(COL_TAC_GREEN), COL_TAC_GREEN);
        tft.drawCentreString("ALL CLEAR", x + 35, curY + 3, 1);

        char cntBuf[28];
        snprintf(cntBuf, sizeof(cntBuf), "WiFi:%d  BLE:%d", wifiCnt, bleCnt);
        tft.setTextColor(muted, COL_HEADER_BG);
        tft.drawRightString(cntBuf, x + w - 4, curY + 3, 1);
    } else {
        char alHdr[16];
        snprintf(alHdr, sizeof(alHdr), "!%u ALERTS", (unsigned)alerts.size());
        int bw = (int)strlen(alHdr) * LW + 10;
        tft.fillRoundRect(x + 3, curY + 1, bw, LH + 4, 2, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawCentreString(alHdr, x + 3 + bw / 2, curY + 3, 1);

        // Only print non-zero alert categories (zero clutter)
        String breakdown;
        if (twins > 0) breakdown += "Twin:" + String(twins) + " ";
        if (drones > 0) breakdown += "UAV:" + String(drones) + " ";
        if (hacks > 0) breakdown += "Tool:" + String(hacks) + " ";
        if (trackers + stalkers > 0) breakdown += "Trk:" + String(trackers + stalkers);
        breakdown.trim();
        tft.setTextColor(COL_QIANKUN_GOLD, COL_HEADER_BG);
        tft.drawRightString(breakdown, x + w - 4, curY + 3, 1);
    }
    curY += LH + 9;

    // 2) Active Flagged Devices List (interactive: UP/DOWN + tap/SEL to lock & track)
    if (!alerts.empty()) {
        ctx.alertCursor = constrain(ctx.alertCursor, 0, (int)alerts.size() - 1);
        const bool showDetail = (h >= 105);
        const int detailH = showDetail ? (LH * 2 + 6) : 0;
        const int listBot = y + h - detailH - (showDetail ? 2 : 0);

        am.rowsTop = curY;
        am.rowH = (h >= 120) ? (LH + 5) : (LH + 3);
        am.visRows = max(1, (listBot - curY) / am.rowH);
        if (ctx.alertCursor < ctx.alertScroll) ctx.alertScroll = ctx.alertCursor;
        if (ctx.alertCursor >= ctx.alertScroll + am.visRows) ctx.alertScroll = ctx.alertCursor - am.visRows + 1;
        ctx.alertScroll = constrain(ctx.alertScroll, 0, max(0, (int)alerts.size() - am.visRows));

        for (int r = 0; r < am.visRows && (ctx.alertScroll + r) < (int)alerts.size(); ++r) {
            int idx = ctx.alertScroll + r;
            const RadarTarget &t = alerts[idx];
            bool sel = (idx == ctx.alertCursor);
            bool locked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
            uint16_t rowBg = sel ? COL_ROW_SEL : bg;
            if (sel) {
                tft.fillRect(x, curY, w, am.rowH - 1, rowBg);
                tft.fillRect(x, curY, 2, am.rowH - 1, TFT_RED);
            }

            uint16_t col = categoryColor(t.category, t.flags);
            const char *badge = alertBadgeShort(t.flags);
            if (!badge) badge = "ALRT";
            int badgeW = 30;
            tft.fillRoundRect(x + 4, curY + 1, badgeW, am.rowH - 3, 2, col);
            tft.setTextColor(uiTextOnAccentColor(col), col);
            tft.drawCentreString(badge, x + 4 + badgeW / 2, curY + 2, 1);

            String rightVal = String(t.rssi) + " " + formatDistShort(t.distM);
            int rightW = (int)rightVal.length() * LW + 4;
            int nameX = x + badgeW + 8;
            int nameChars = max(4, (w - (nameX - x) - rightW - 4) / LW);
            String nm = (locked ? "*" : "") + targetDisplayName(t);
            if ((int)nm.length() > nameChars) nm = nm.substring(0, nameChars);

            tft.setTextColor(locked ? COL_QIANKUN_GOLD : bright, rowBg);
            tft.drawString(nm, nameX, curY + 2, 1);
            tft.setTextColor(sel ? bright : muted, rowBg);
            tft.drawRightString(rightVal, x + w - 3, curY + 2, 1);
            curY += am.rowH;
        }

        if (showDetail) {
            const RadarTarget &st = alerts[ctx.alertCursor];
            int dy = y + h - detailH;
            tft.fillRect(x, dy, w, detailH, COL_PANEL_BG);
            tft.drawFastHLine(x, dy, w, uiBorderDimColor());
            tft.setTextColor(TFT_ORANGE, COL_PANEL_BG);
            tft.drawString(
                String(alertReasonText(st.flags)).substring(0, max(10, (w - 8) / LW)), x + 4, dy + 3, 1
            );
            char foot[44];
            snprintf(
                foot,
                sizeof(foot),
                "%s  |  Tap/SEL = Fox-Hunt",
                formatMac(st.mac).c_str()
            );
            tft.setTextColor(muted, COL_PANEL_BG);
            tft.drawString(String(foot).substring(0, max(10, (w - 8) / LW)), x + 4, dy + LH + 5, 1);
        }
        return am;
    }

    // 3) No active alerts: clean RF environment overview + recent security events (if any)
    int cardH = (h >= 110) ? (LH * 3 + 16) : (LH * 2 + 10);
    tft.fillRect(x, curY, w, cardH, COL_PANEL_BG);
    tft.drawRect(x, curY, w, cardH, uiBorderDimColor());
    int iy = curY + 4;

    char l1[48];
    snprintf(l1, sizeof(l1), "WiFi: %d  (Open:%d  Hidden:%d)", wifiCnt, openCnt, hiddenCnt);
    tft.setTextColor(bright, COL_PANEL_BG);
    tft.drawString(String(l1).substring(0, max(10, (w - 10) / LW)), x + 6, iy, 1);
    iy += LH + 4;

    char l2[48];
    if (topVendor) {
        snprintf(l2, sizeof(l2), "BLE: %d  |  Top: %s (%d)", bleCnt, topVendor, topVendorCnt);
    } else {
        snprintf(l2, sizeof(l2), "BLE: %d  |  Range: %.0fm", bleCnt, ctx.rangeMaxM);
    }
    tft.setTextColor(muted, COL_PANEL_BG);
    tft.drawString(String(l2).substring(0, max(10, (w - 10) / LW)), x + 6, iy, 1);
    iy += LH + 4;

    if (h >= 110) {
        char l3[48];
        snprintf(
            l3,
            sizeof(l3),
            "Noise Filter: %s  |  Range: %.0fm",
            ctx.noiseFilter ? "ON" : "OFF",
            ctx.rangeMaxM
        );
        tft.setTextColor(muted, COL_PANEL_BG);
        tft.drawString(String(l3).substring(0, max(10, (w - 10) / LW)), x + 6, iy, 1);
    }
    curY += cardH + 6;

    // Recent security-only events (if space remains)
    int logH = (y + h) - curY - 1;
    if (logH < LH + 8) return am;
    if (ctx.eventCount == 0) {
        tft.setTextColor(muted, bg);
        tft.drawCentreString(bruce_i18n::tr("No EW anomalies logged."), x + w / 2, curY + 8, 1);
        return am;
    }

    tft.setTextColor(muted, bg);
    tft.drawString("Recent Alerts:", x + 2, curY, 1);
    curY += LH + 3;
    int rowH = LH + 3;
    int maxLines = max(1, (y + h - curY) / rowH);
    int maxChars = max(10, (w - 6) / LW);
    for (int i = 0; i < (int)ctx.eventCount && i < maxLines; ++i) {
        size_t idx = (ctx.eventHead + RADAR_MAX_EVENTS - 1 - (size_t)i) % RADAR_MAX_EVENTS;
        const RadarEvent &ev = ctx.events[idx];
        uint32_t dtSec = (ev.tsMs >= ctx.startMs) ? (ev.tsMs - ctx.startMs) / 1000u : 0;
        char row[48];
        snprintf(row, sizeof(row), "[%02lu:%02lu] %s", (unsigned long)(dtSec / 60), (unsigned long)(dtSec % 60), ev.text);
        tft.setTextColor(ev.col, bg);
        tft.drawString(String(row).substring(0, maxChars), x + 2, curY, 1);
        curY += rowH;
    }
    return am;
}

/*
 * Detail card for a single selected device.
 * showDeviceInspector shows the chosen target's full info (BSSID, vendor,
 * signal, channel, encryption) and actions, exited with a key press.
 */
static void showDeviceInspector(
    RadarContext &ctx, const RadarTarget &t, bool &wantTriangulate, bool &wantFoxHunt
) {
    drawMainBorderWithTitle("Device Info");
    const int x = BORDER_PAD_X + 6;
    int y = BORDER_PAD_Y + FM * LH + 5;
    const int rowH = (FP == 1 && bruce_i18n::isChinese()) ? 13 : (LH + 3);

    tft.setTextSize(FP);
    uint16_t col = categoryColor(t.category, t.flags);
    drawCategoryGlyph(x + 3, y + 4, t.category, col, t.flags);
    tft.setTextColor(col, bruceConfig.bgColor);
    String typeLine = String(t.type == RadarTargetType::WIFI ? "WiFi: " : "BLE: ") +
                      bruce_i18n::tr(categoryName(t.category));
    if (t.subtype[0]) typeLine += " (" + String(t.subtype) + ")";
    tft.drawString(typeLine, x + 10, y, 1);
    y += rowH;

    auto drawKV = [&](const char *k, const String &v, uint16_t vCol = TFT_WHITE) {
        if (y + rowH > tftHeight - BORDER_PAD_X - LH - 3) return;
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        String ks = String(bruce_i18n::tr(k)) + ": ";
        tft.drawString(ks, x, y, 1);
        int kw = bruce_i18n::utf8PixelWidth(ks, FP);
        tft.setTextColor(vCol, bruceConfig.bgColor);
        tft.drawString(v, x + kw, y, 1);
        y += rowH;
    };

    const char *oui = lookupOuiVendor(t.mac);
    String nameVal = t.name[0] ? String(t.name) : String("<unnamed>");
    if (oui) nameVal += " [" + String(oui) + "]";
    drawKV("Name", nameVal);
    drawKV("MAC", formatMac(t.mac), TFT_LIGHTGREY);

    String radioVal = String(t.rssi) + "dBm (pk:" + String(t.peakRssi) + ") ~" + formatDistShort(t.distM);
    if (t.type == RadarTargetType::WIFI) {
        radioVal += " C" + String(t.channel) + " " + wifiAuthShort(t.authMode);
    } else {
        radioVal += " TX:" + String(t.refRssi1m);
    }
    drawKV("Signal", radioVal, TFT_GREEN);

    String seenVal = String(t.seenCount) + " pkts  " + formatDwell(t.firstSeenMs, millis());
    if (t.flags & RFLAG_EVIL_TWIN) seenVal += "  [EVIL-TWIN!]";
    else if (t.flags & RFLAG_DRONE) seenVal += "  [UAV/DRONE!]";
    else if (t.flags & RFLAG_STALKER) seenVal += "  [STALKER!]";
    else if (t.flags & RFLAG_HACK_DEV) seenVal += "  [RF-TOOL!]";
    else if (t.flags & RFLAG_TRACKER) seenVal += "  [TRACKER!]";
    drawKV("Intel", seenVal, (t.flags ? TFT_RED : TFT_CYAN));

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

    double tLat = t.lat, tLon = t.lon;
    bool hasTargetGps = t.hasGps;
    if (!hasTargetGps && bruceLocation.estimateTargetGps(t.relX, t.relY, tLat, tLon)) {
        hasTargetGps = true;
    }
    if (hasTargetGps) {
        char gpsBuf[42];
        snprintf(gpsBuf, sizeof(gpsBuf), "%.5f,%.5f (%s)", tLat, tLon, bruceLocation.source().c_str());
        drawKV("GPS", String(gpsBuf), TFT_CYAN);
    } else {
        drawKV("GPS", bruce_i18n::tr("No GPS fix"), TFT_DARKGREY);
    }

    printCenterFootnote("SEL=Fox-Hunt  UP=Triangulate  ESC=Back");

    check(SelPress);
    check(EscPress);
    check(UpPress);
    check(PrevPress);

    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (check(UpPress) || check(PrevPress)) {
            lockOnTarget(ctx, t);
            wantTriangulate = true;
            break;
        }
        if (check(SelPress)) {
            lockOnTarget(ctx, t);
            wantFoxHunt = true;
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
    // Always use a local vector so we NEVER invalidate an outer menu's executing lambda!
    std::vector<Option> pickOpts;
    size_t limit = min<size_t>(list.size(), 30);
    pickOpts.reserve(limit + 1);
    for (size_t i = 0; i < limit; ++i) {
        const RadarTarget t = list[i];
        String prefix = (t.flags & RFLAG_ANY_ALERT)
                            ? "[!] "
                            : (t.type == RadarTargetType::WIFI ? "[W] " : "[B] ");
        String label = prefix + targetDisplayName(t) + " " + formatDistShort(t.distM);
        pickOpts.push_back({label, [&outTarget, &picked, t]() {
            outTarget = t;
            picked = true;
        }});
    }
    pickOpts.push_back({"Back", []() {}});
    loopOptions(pickOpts, MENU_TYPE_SUBMENU, title);
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
    f.println("ms,radio,category,subtype,vendor,name,mac,auth,ch,rssi,peak_rssi,dist_m,pkts,dwell_s,flags,rel_x,rel_y,lat,lon");
    uint32_t now = millis();
    for (const auto &t : list) {
        const char *oui = lookupOuiVendor(t.mac);
        double outLat = t.hasGps ? t.lat : 0.0;
        double outLon = t.hasGps ? t.lon : 0.0;
        if (!t.hasGps) bruceLocation.estimateTargetGps(t.relX, t.relY, outLat, outLon);
        f.printf(
            "%lu,%s,%s,%s,%s,\"%s\",%s,%s,%u,%d,%d,%.2f,%u,%lu,0x%02X,%.2f,%.2f,%.6f,%.6f\n",
            (unsigned long)t.lastSeenMs,
            t.type == RadarTargetType::WIFI ? "WIFI" : "BLE",
            categoryName(t.category),
            t.subtype,
            oui ? oui : "",
            t.name,
            formatMac(t.mac).c_str(),
            t.type == RadarTargetType::WIFI ? wifiAuthShort(t.authMode) : "BLE",
            (unsigned)t.channel,
            (int)t.rssi,
            (int)t.peakRssi,
            t.distM,
            (unsigned)t.seenCount,
            (unsigned long)((now >= t.firstSeenMs) ? (now - t.firstSeenMs) / 1000u : 0),
            (unsigned)t.flags,
            t.relX,
            t.relY,
            outLat,
            outLon
        );
    }
    f.close();
    displaySuccess(String("Saved ") + String(list.size()) + " -> /radar_log.csv", true);
}

static bool sampleTargetDistance(
    RadarContext &ctx, const RadarTarget &target, bool timeSliced, bool &wifiAvailable, bool &bleAvailable,
    NimBLEScan *&radarScan, float &outDistM, int8_t &outAvgRssi, int &outNearbyBle
) {
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

        printCenterFootnote("SEL=Sample Point  ESC=Cancel");

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
        if (!gotSample) displayWarning("Weak signal: using last RSSI", true);
    }

    float xEst = (r[0] * r[0] - r[1] * r[1] + D * D) / (2.0f * D);
    float yEst = (r[0] * r[0] - r[2] * r[2] + D * D) / (2.0f * D);
    float distEst = sqrtf(xEst * xEst + yEst * yEst);
    float brgDeg = vectorToBearingDeg(xEst, yEst);

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

    drawMainBorderWithTitle("Triangulation Result");
    int top = BORDER_PAD_Y + FM * LH + 4;
    int bot = tftHeight - BORDER_PAD_X - LH - 4;
    int h = max(40, bot - top);
    int mapR = min(h / 2 - 4, (tftWidth / 2 - BORDER_PAD_X - 8) / 2);
    int mapCx = BORDER_PAD_X + mapR + 8;
    int mapCy = top + h / 2;

    float maxCoord = max(D + 1.0f, max(fabsf(xEst), fabsf(yEst)) * 1.25f);
    float scalePx = (float)(mapR - 4) / maxCoord;

    tft.drawCircle(mapCx, mapCy, mapR, COL_GRID_MID);
    tft.drawFastHLine(mapCx - mapR, mapCy, 2 * mapR + 1, COL_GRID_DIM);
    tft.drawFastVLine(mapCx, mapCy - mapR, 2 * mapR + 1, COL_GRID_DIM);

    int p1x = mapCx, p1y = mapCy;
    int p2x = mapCx + (int)(D * scalePx), p2y = mapCy;
    int p3x = mapCx, p3y = mapCy - (int)(D * scalePx);
    tft.fillCircle(p1x, p1y, 2, TFT_CYAN);
    tft.fillCircle(p2x, p2y, 2, TFT_CYAN);
    tft.fillCircle(p3x, p3y, 2, TFT_CYAN);

    int tx = constrain(mapCx + (int)(xEst * scalePx), mapCx - mapR + 3, mapCx + mapR - 3);
    int ty = constrain(mapCy - (int)(yEst * scalePx), mapCy - mapR + 3, mapCy + mapR - 3);
    tft.drawLine(p1x, p1y, tx, ty, TFT_YELLOW);
    tft.drawCircle(tx, ty, 5, TFT_RED);
    tft.fillCircle(tx, ty, 2, TFT_YELLOW);

    int ix = mapCx + mapR + 10;
    int iy = top + 2;
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
    iy += LH + 3;

    if (ctx.gpsFix) {
        snprintf(buf, sizeof(buf), "%.4f,%.4f", ctx.gpsLat, ctx.gpsLon);
        tft.drawString(buf, ix, iy, 1);
    }

    printCenterFootnote("Press any key to return");

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

struct RadarTouchZoneGuard {
    bool prev = true;
    RadarTouchZoneGuard() {
#if defined(HAS_TOUCH)
        prev = touchZoneOutsideFooterEnabled;
        touchZoneOutsideFooterEnabled = false;
#endif
    }
    ~RadarTouchZoneGuard() {
#if defined(HAS_TOUCH)
        touchZoneOutsideFooterEnabled = prev;
#endif
    }
};

} // namespace

void radar_setup(RadarInitialMode initialMode, RadarInitialView initialView) {
    RadarContext ctx;
    ctx.startMs = millis();
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

    RadarTouchZoneGuard touchGuard;
    bruceLocation.ensureLoaded();

    TinyGPSPlus gpsParser;
    if (gpsPinsSafeToOpen()) {
        ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
        Serial2.begin(
            bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx, bruceConfigPins.gps_bus.tx
        );
        ctx.gpsStarted = true;
    }

    RadarMode mode = static_cast<RadarMode>(initialMode);
    RadarView view = static_cast<RadarView>(initialView);

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

#if defined(HAS_TOUCH)
    const bool hasSoftkeyBar = (tftWidth >= 240 && tftHeight >= 180);
#else
    const bool hasSoftkeyBar = false;
#endif
    const int softkeyH = hasSoftkeyBar ? 18 : 0;
    const int softkeyY = tftHeight - BORDER_PAD_X - softkeyH;
    const int contentTop = uiContentTop();
    const int contentBot = hasSoftkeyBar ? (softkeyY - 3) : uiContentBottom(true);
    const int contentH = max(40, contentBot - contentTop);
    const int contentL = BORDER_PAD_X + 2;
    const int contentW = tftWidth - 2 * BORDER_PAD_X - 4;

    const bool wideLayout = (contentW >= contentH + 88);
    const int scopeR = wideLayout ? (min(contentH, contentW / 2) / 2 - 4) : (contentH / 2 - 4);
    const int scopeCx = wideLayout ? (contentL + scopeR + 5) : (tftWidth / 2);
    const int scopeCy = contentTop + contentH / 2;
    const int hudX = scopeCx + scopeR + 8;
    const int hudW = (contentL + contentW) - hudX;

    // Tear-free offscreen PSRAM sprite buffer for the PPI radar dish (zero flicker on T-Deck / PSRAM boards)
#if !defined(BRUCE_COMPACT_CJK_FONT)
    tft_sprite scopeSpr(&tft);
    bool scopeSprReady = false;
    const int sprSize = (scopeR + 3) * 2 + 1;
    if (psramFound() && sprSize > 20 && sprSize <= 240) {
        scopeSpr.setColorDepth(16);
        scopeSprReady = (scopeSpr.createSprite(sprSize, sprSize) != nullptr);
    }
#endif

    ScopeHudMetrics hudMetrics;
    TableViewMetrics tableMetrics;
    AlertViewMetrics alertMetrics;

    auto drawSoftkeyBar = [&]() {
        if (!hasSoftkeyBar) return;
        const int nBtns = 6;
        const int gap = 3;
        const int btnW = (contentW - gap * (nBtns - 1)) / nBtns;
        char zoomLbl[8];
        snprintf(zoomLbl, sizeof(zoomLbl), "%.0fm", ctx.rangeMaxM);
        const String labels[6] = {
            bruce_i18n::tr("View"),
            String(zoomLbl),
            String(modeShort(mode)),
            bruce_i18n::tr(ctx.hasLocked ? "Unlock" : "Lock"),
            bruce_i18n::tr("Menu"),
            bruce_i18n::tr("Exit"),
        };
        const uint16_t accents[6] = {
            bruceConfig.priColor,
            bruceConfig.secColor,
            TFT_CYAN,
            ctx.hasLocked ? TFT_ORANGE : COL_TAC_GREEN,
            bruceConfig.secColor,
            TFT_RED,
        };
        const uint16_t surf = uiSurfaceColor();
        const uint16_t bright = uiTextBrightColor();
        tft.fillRect(contentL, softkeyY, contentW, softkeyH, bruceConfig.bgColor);
        tft.setTextSize(1);
        for (int i = 0; i < nBtns; ++i) {
            int bx = contentL + i * (btnW + gap);
            int bw = (i == nBtns - 1) ? (contentL + contentW - bx) : btnW;
            tft.fillRoundRect(bx, softkeyY, bw, softkeyH, 4, surf);
            tft.drawRoundRect(bx, softkeyY, bw, softkeyH, 4, uiBorderDimColor());
            tft.fillRect(bx + 3, softkeyY + 1, bw - 6, 2, accents[i]);
            tft.setTextColor(bright, surf);
            String fit = bruce_i18n::utf8TruncateToWidth(labels[i], bw - 4, 1);
            int th = bruce_i18n::containsNonAscii(fit) ? 12 : LH;
            tft.drawCentreString(fit, bx + bw / 2, softkeyY + (softkeyH - th) / 2 + 1, 1);
        }
    };

    auto drawLayout = [&]() {
        drawMainBorderWithTitle(viewName(view, ctx.hasLocked));
        if (hasSoftkeyBar) {
            drawSoftkeyBar();
        } else {
#if defined(HAS_3_BUTTONS)
            const char *hint = (view == RadarView::TABLE) ? "PREV/NEXT=Row  SEL=Menu"
                                                          : "PREV/NEXT=View  SEL=Menu";
#else
            const char *hint = (view == RadarView::FOX_SPEC) ? "LR=View  UP/DN=Filter  SEL=Menu"
                                                             : "UP/DN=Select  LR=View  SEL=Menu";
#endif
            printCenterFootnote(hint);
        }
    };

    drawLayout();

    uint32_t lastFrameMs = 0;
    uint32_t lastHudMs = 0;
    uint32_t lastFoxSampleMs = 0;
    uint32_t lastLocSyncMs = 0;
    uint32_t lastContactBeepMs = 0;
    size_t prevAlertCount = 0;
    bool exitRadar = false;
    bool openMenuReq = false;

    auto radarEmitBeep = [&](unsigned int freq, unsigned long durMs, short waveType = 2) {
        if (!bruceConfig.radarBeep || !bruceConfig.soundEnabled) return;
#if defined(HAS_SPEAKER)
        if (isAudioPlaying()) return;
        // Ensure duration exceeds MAX98357A I2S amp wakeup (~25ms) so every ping rings crisply
        unsigned long safeDur = (durMs < 95) ? 95 : durMs;
        playTone(freq, safeDur, waveType, PLAYBACK_ASYNC);
#else
        _tone(freq, durMs, PLAYBACK_ASYNC);
#endif
    };

    auto radarEmitLockChirp = [&]() {
        // Single seamless dual-chirp buffer (waveType=3) so the 2nd tone is never dropped by isAudioPlaying()
        radarEmitBeep(1320, 140, 3);
    };

    auto cycleView = [&](int dir) {
        int next = ((int)view + dir + 4) % 4;
        view = static_cast<RadarView>(next);
        drawLayout();
        lastHudMs = 0;
    };

    auto cycleZoom = [&]() {
        if (ctx.rangeMaxM <= 12.0f) ctx.rangeMaxM = 30.0f;
        else if (ctx.rangeMaxM <= 35.0f) ctx.rangeMaxM = 60.0f;
        else ctx.rangeMaxM = 10.0f;
        drawSoftkeyBar();
        lastHudMs = 0;
    };

    auto cycleFilter = [&](int dir) {
        stopAllScans();
#if defined(HAS_SPEAKER)
        if (isAudioPlaying()) stopAudioPlayback();
#endif
        int next = ((int)mode + dir + 5) % 5;
        mode = static_cast<RadarMode>(next);
        nextIsBle = (mode == RadarMode::BLE_ONLY);
        ctx.scopeCursor = 0;
        ctx.scopeScroll = 0;
        ctx.tableCursor = 0;
        ctx.tableScroll = 0;
        ctx.hasFocused = false;
        lastSweepEndMs = 0;
        lastHudMs = 0;
        drawSoftkeyBar();
    };

    auto lockFocusedOrPriority = [&]() {
        if (ctx.hasLocked) {
            ctx.hasLocked = false;
            drawSoftkeyBar();
            lastHudMs = 0;
            radarEmitBeep(920, 45);
            return;
        }
        std::vector<RadarTarget> cand = snapshotTargets(ctx, mode);
        if (cand.empty()) return;

        const RadarTarget *chosen = nullptr;
        if (view == RadarView::THREAT) {
            std::vector<RadarTarget> alerts = extractAlertTargets(snapshotTargets(ctx, mode, true));
            if (!alerts.empty()) {
                int idx = constrain(ctx.alertCursor, 0, (int)alerts.size() - 1);
                lockOnTarget(ctx, alerts[idx]);
                drawSoftkeyBar();
                lastHudMs = 0;
                radarEmitLockChirp();
                return;
            }
        }
        if (ctx.hasFocused) {
            for (const auto &t : cand) {
                if (isSameTarget(t, ctx.focusedType, ctx.focusedMac)) {
                    chosen = &t;
                    break;
                }
            }
        }
        if (!chosen) chosen = &cand[0]; // snapshotTargets already sorts alerts first, then closest
        lockOnTarget(ctx, *chosen);
        drawSoftkeyBar();
        lastHudMs = 0;
        radarEmitLockChirp();
    };

    auto stepSelection = [&](int delta) {
        if (view == RadarView::THREAT) {
            std::vector<RadarTarget> alerts = extractAlertTargets(snapshotTargets(ctx, mode, true));
            if (!alerts.empty()) {
                ctx.alertCursor = constrain(ctx.alertCursor + delta, 0, (int)alerts.size() - 1);
                lastHudMs = 0;
            }
            return;
        }
        std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
        if (visible.empty()) return;
        int &cur = (view == RadarView::TABLE) ? ctx.tableCursor : ctx.scopeCursor;
        syncCursorToFocus(ctx, visible, cur);
        cur = constrain(cur + delta, 0, (int)visible.size() - 1);
        setFocusedTarget(ctx, visible[cur]);
        lastHudMs = 0;
    };

    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);
#if defined(HAS_TOUCH)
    inputLock();
    touchPoint.Clear();
    inputUnlock();
#endif

    while (!exitRadar && !returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();

        // 0) Non-blocking GPS NMEA feed -> updates global bruceLocation state
        if (ctx.gpsStarted) {
            int budget = 64;
            while (Serial2.available() > 0 && budget-- > 0) gpsParser.encode(Serial2.read());
            if (gpsParser.location.isValid() && gpsParser.location.age() < 5000) {
                ctx.gpsFix = true;
                ctx.gpsLat = gpsParser.location.lat();
                ctx.gpsLon = gpsParser.location.lng();
                ctx.gpsSats = gpsParser.satellites.value();
                bruceLocation.updateGps(
                    ctx.gpsLat, ctx.gpsLon, gpsParser.altitude.meters(), ctx.gpsSats
                );
            } else {
                ctx.gpsFix = false;
            }
        }

        // 0b) Periodic RF-anchor positioning & whole-app location synchronization (~every 4s)
        if (now - lastLocSyncMs >= 4000) {
            lastLocSyncMs = now;
            std::vector<RadarTarget> allNow = snapshotTargets(ctx, mode, true);
            if (!allNow.empty()) {
                if (!ctx.gpsFix && !bruceLocation.anchors().empty()) {
                    std::vector<RfObservation> obs;
                    obs.reserve(allNow.size());
                    for (const auto &t : allNow) {
                        RfObservation o;
                        memcpy(o.mac, t.mac, 6);
                        o.isWifi = (t.type == RadarTargetType::WIFI);
                        o.rssi = t.rssi;
                        o.distM = t.distM;
                        obs.push_back(o);
                    }
                    int matched = 0;
                    bruceLocation.tryRfPositionFix(obs, matched);
                }
                if (bruceLocation.hasLocation()) {
                    size_t recLimit = min<size_t>(allNow.size(), 10);
                    for (size_t i = 0; i < recLimit; ++i) {
                        double tLat = 0.0, tLon = 0.0;
                        if (bruceLocation.estimateTargetGps(allNow[i].relX, allNow[i].relY, tLat, tLon)) {
                            bruceLocation.recordAnchor(
                                allNow[i].mac,
                                allNow[i].type == RadarTargetType::WIFI,
                                targetDisplayName(allNow[i]).c_str(),
                                tLat,
                                tLon,
                                allNow[i].rssi
                            );
                        }
                    }
                }
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
                    uint8_t flags = 0;
                    RadarCategory cat = RadarCategory::WIFI_SECURED;
                    if (ssid.isEmpty()) {
                        cat = RadarCategory::WIFI_HIDDEN;
                    } else if (auth == WIFI_AUTH_OPEN) {
                        cat = RadarCategory::WIFI_OPEN;
                        flags |= RFLAG_WEAK_SEC;
                    } else if (auth == WIFI_AUTH_WEP) {
                        cat = RadarCategory::WIFI_WEP;
                        flags |= RFLAG_WEAK_SEC;
                    }
                    radarUpsertTarget(
                        &ctx,
                        RadarTargetType::WIFI,
                        cat,
                        flags,
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
                detectEvilTwins(ctx);
                wifiScanning = false;
                lastSweepEndMs = now;
                if (mode != RadarMode::WIFI_ONLY && mode != RadarMode::OPEN_WIFI) nextIsBle = true;
            } else if (n == WIFI_SCAN_FAILED) {
                WiFi.scanDelete();
                wifiScanning = false;
                lastSweepEndMs = now;
                if (mode != RadarMode::WIFI_ONLY && mode != RadarMode::OPEN_WIFI) nextIsBle = true;
            }
        }

        // 2) Check completed BLE scan
        if (bleScanning && radarScan && !radarScan->isScanning()) {
            radarScan->clearResults();
            bleScanning = false;
            lastSweepEndMs = now;
            if (mode != RadarMode::BLE_ONLY) nextIsBle = false;
        }

        // 3) Launch next sweep (coexisting or time-sliced; prioritize locked radio in Fox-Hunt!)
        if (!wifiScanning && !bleScanning && (now - lastSweepEndMs >= 200)) {
            bool wantBle = false;
            bool wantWifi = false;
            if (view == RadarView::FOX_SPEC && ctx.hasLocked) {
                wantBle = (ctx.lockedType == RadarTargetType::BLE);
                wantWifi = (ctx.lockedType == RadarTargetType::WIFI);
            } else if (mode == RadarMode::BLE_ONLY) {
                wantBle = true;
            } else if (mode == RadarMode::WIFI_ONLY || mode == RadarMode::OPEN_WIFI) {
                wantWifi = true;
            } else {
                wantBle = nextIsBle;
                wantWifi = !nextIsBle;
            }

            if (wantBle) {
                if (ensureBleReady() && radarScan->start(RADAR_BLE_SCAN_MS, false, true)) {
                    bleScanning = true;
                } else {
                    lastSweepEndMs = now;
                    nextIsBle = false;
                }
            } else if (wantWifi) {
                if (ensureWifiReady()) {
                    int16_t res = WiFi.scanNetworks(true, true, false, 110);
                    if (res == WIFI_SCAN_RUNNING) wifiScanning = true;
                    else {
                        lastSweepEndMs = now;
                        nextIsBle = true;
                    }
                } else {
                    lastSweepEndMs = now;
                    nextIsBle = true;
                }
            }
        }

        // 4) Update Fox-Hunt sparkline + optional Geiger audio ticks
        if (ctx.hasLocked && (now - lastFoxSampleMs >= 450)) {
            lastFoxSampleMs = now;
            int8_t curRssi = -100;
            bool foundLocked = false;
            portENTER_CRITICAL(&ctx.mux);
            for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
                const RadarTarget &slot = ctx.targets[i];
                if (slot.active && slot.type == ctx.lockedType &&
                    memcmp(slot.mac, ctx.lockedMac, 6) == 0) {
                    curRssi = slot.rssi;
                    foundLocked = true;
                    break;
                }
            }
            portEXIT_CRITICAL(&ctx.mux);

            if (foundLocked) {
                if (curRssi > ctx.foxPeakRssi) ctx.foxPeakRssi = curRssi;
                if (ctx.foxHistCount < RADAR_HIST_LEN) {
                    ctx.foxHist[ctx.foxHistCount++] = curRssi;
                } else {
                    memmove(ctx.foxHist, ctx.foxHist + 1, RADAR_HIST_LEN - 1);
                    ctx.foxHist[RADAR_HIST_LEN - 1] = curRssi;
                }
                if (ctx.geigerAudio && bruceConfig.radarBeep && bruceConfig.soundEnabled) {
                    int pct = constrain((curRssi + 95) * 100 / 60, 0, 100);
                    uint32_t interval = (uint32_t)constrain(920 - pct * 8, 140, 920);
                    if (now - ctx.lastGeigerMs >= interval) {
                        ctx.lastGeigerMs = now;
                        unsigned int freq = 1200 + (unsigned int)pct * 16;
                        radarEmitBeep(freq, 35);
                    }
                }
            }
        }

        // 5) Render active view + alert-only audible notification (no idle sweep spam)
        auto checkAlertBeep = [&](const std::vector<RadarTarget> &list) {
            size_t alerts = 0;
            for (const auto &t : list) {
                if (t.flags & RFLAG_ANY_ALERT) ++alerts;
            }
            if (alerts > prevAlertCount && bruceConfig.radarBeep && bruceConfig.soundEnabled &&
                (now - lastContactBeepMs >= 3000)) {
                lastContactBeepMs = now;
                radarEmitBeep(1680, 160, 3); // Dual-tone alert chirp on new threat
            }
            prevAlertCount = alerts;
        };

        if (view == RadarView::SCOPE) {
            if (now - lastFrameMs >= 65) {
                pruneStaleTargets(ctx);
                sweepRad += 0.135f;
                if (sweepRad >= 2.0f * RADAR_PI) sweepRad -= 2.0f * RADAR_PI;

                std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                syncCursorToFocus(ctx, visible, ctx.scopeCursor);
#if !defined(BRUCE_COMPACT_CJK_FONT)
                drawRadarScope(
                    scopeSprReady ? &scopeSpr : nullptr, scopeCx, scopeCy, scopeR, sweepRad, visible, ctx
                );
#else
                drawRadarScope(nullptr, scopeCx, scopeCy, scopeR, sweepRad, visible, ctx);
#endif
                checkAlertBeep(visible);
                lastFrameMs = now;

                if (wideLayout && (now - lastHudMs >= 300 || lastHudMs == 0)) {
                    hudMetrics = drawRadarHud(
                        hudX, contentTop, hudW, contentH, mode, wifiScanning, bleScanning, visible, ctx
                    );
                    lastHudMs = now;
                }
            }
        } else if (now - lastHudMs >= 350 || lastHudMs == 0) {
            pruneStaleTargets(ctx);
            if (view == RadarView::TABLE) {
                std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                tableMetrics = drawIntelTable(
                    contentL, contentTop, contentW, contentH, mode, wifiScanning, bleScanning, visible, ctx
                );
                checkAlertBeep(visible);
            } else if (view == RadarView::FOX_SPEC) {
                std::vector<RadarTarget> allList = snapshotTargets(ctx, mode, true);
                drawFoxHuntOrSpectrum(contentL, contentTop, contentW, contentH, allList, ctx);
            } else {
                std::vector<RadarTarget> allList = snapshotTargets(ctx, mode, true);
                alertMetrics = drawThreatConsole(contentL, contentTop, contentW, contentH, allList, ctx);
                checkAlertBeep(allList);
            }
            lastHudMs = now;
        }

        // 6) Direct Touch Interaction (6-Button Softkeys + Scope Blip/HUD Tap + Table/Alerts Tap)
#if defined(HAS_TOUCH)
        bool gotTap = false;
        int tx = 0, ty = 0;
        inputLock();
        if (touchPoint.pressed) {
            tx = touchPoint.x;
            ty = touchPoint.y;
            touchPoint.Clear();
            gotTap = true;
        }
        inputUnlock();

        if (gotTap) {
            if (hasSoftkeyBar && ty >= softkeyY - 3 && ty <= tftHeight) {
                int slot = constrain(((tx - contentL) * 6) / max(1, contentW), 0, 5);
                switch (slot) {
                    case 0: cycleView(1); break;
                    case 1: cycleZoom(); break;
                    case 2: cycleFilter(1); break;
                    case 3: lockFocusedOrPriority(); break;
                    case 4: openMenuReq = true; break;
                    case 5: exitRadar = true; break;
                }
            } else if (view == RadarView::SCOPE) {
                int dx = tx - scopeCx;
                int dy = ty - scopeCy;
                if (dx * dx + dy * dy <= (scopeR + 10) * (scopeR + 10)) {
                    std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                    int bestIdx = -1;
                    int bestD2 = 22 * 22;
                    for (int i = 0; i < (int)visible.size(); ++i) {
                        const RadarTarget &t = visible[i];
                        bool isLocked = ctx.hasLocked && isSameTarget(t, ctx.lockedType, ctx.lockedMac);
                        if (t.distM > ctx.rangeMaxM && !(t.flags & RFLAG_ANY_ALERT) && !isLocked) continue;
                        int distPx = distToRadius(t.distM, scopeR, ctx.rangeMaxM);
                        float ang = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
                        int bx = scopeCx + (int)(cosf(ang) * distPx);
                        int by = scopeCy + (int)(sinf(ang) * distPx);
                        int d2 = (tx - bx) * (tx - bx) + (ty - by) * (ty - by);
                        if (d2 < bestD2) {
                            bestD2 = d2;
                            bestIdx = i;
                        }
                    }
                    if (bestIdx >= 0) {
                        bool alreadyFocused =
                            ctx.hasFocused && isSameTarget(visible[bestIdx], ctx.focusedType, ctx.focusedMac);
                        ctx.scopeCursor = bestIdx;
                        setFocusedTarget(ctx, visible[bestIdx]);
                        if (alreadyFocused) {
                            lockOnTarget(ctx, visible[bestIdx]);
                            drawSoftkeyBar();
                            radarEmitLockChirp();
                        }
                        lastHudMs = 0;
                    }
                } else if (wideLayout && tx >= hudX && tx < hudX + hudW) {
                    std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                    if (ty >= hudMetrics.cardY && ty < hudMetrics.cardY + hudMetrics.cardH) {
                        // Tapping focused/locked card toggles lock on it
                        lockFocusedOrPriority();
                    } else if (hudMetrics.visRows > 0 && ty >= hudMetrics.listY && ty < contentBot) {
                        int tappedRow = (ty - hudMetrics.listY) / max(1, hudMetrics.rowH);
                        int idx = ctx.scopeScroll + tappedRow;
                        if (idx >= 0 && idx < (int)visible.size()) {
                            if (ctx.scopeCursor == idx) {
                                lockOnTarget(ctx, visible[idx]);
                                drawSoftkeyBar();
                                radarEmitLockChirp();
                            } else {
                                ctx.scopeCursor = idx;
                                setFocusedTarget(ctx, visible[idx]);
                            }
                            lastHudMs = 0;
                        }
                    }
                }
            } else if (view == RadarView::TABLE) {
                if (tableMetrics.visRows > 0 && ty >= tableMetrics.rowsTop &&
                    ty < tableMetrics.rowsTop + tableMetrics.visRows * tableMetrics.rowH) {
                    int tappedRow = (ty - tableMetrics.rowsTop) / max(1, tableMetrics.rowH);
                    std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                    int idx = ctx.tableScroll + tappedRow;
                    if (idx >= 0 && idx < (int)visible.size()) {
                        if (ctx.tableCursor == idx) {
                            lockOnTarget(ctx, visible[idx]);
                            drawSoftkeyBar();
                            radarEmitLockChirp();
                        } else {
                            ctx.tableCursor = idx;
                            setFocusedTarget(ctx, visible[idx]);
                        }
                        lastHudMs = 0;
                    }
                }
            } else if (view == RadarView::THREAT) {
                if (alertMetrics.visRows > 0 && ty >= alertMetrics.rowsTop &&
                    ty < alertMetrics.rowsTop + alertMetrics.visRows * alertMetrics.rowH) {
                    int tappedRow = (ty - alertMetrics.rowsTop) / max(1, alertMetrics.rowH);
                    std::vector<RadarTarget> alerts = extractAlertTargets(snapshotTargets(ctx, mode, true));
                    int idx = ctx.alertScroll + tappedRow;
                    if (idx >= 0 && idx < (int)alerts.size()) {
                        if (ctx.alertCursor == idx) {
                            lockOnTarget(ctx, alerts[idx]);
                            view = RadarView::FOX_SPEC;
                            drawLayout();
                            radarEmitLockChirp();
                        } else {
                            ctx.alertCursor = idx;
                        }
                        lastHudMs = 0;
                    }
                }
            }
        }
#endif

        // 7) Physical QWERTY Keyboard Hotkeys (T-Deck / Cardputer)
#if defined(HAS_KEYBOARD)
        char hk = checkLetterShortcutPress();
        if (hk > 0) {
            if (hk >= 'A' && hk <= 'Z') hk = (char)(hk - 'A' + 'a');
            switch (hk) {
                case 'v': cycleView(1); break;
                case 'z': cycleZoom(); break;
                case 'f': cycleFilter(1); break;
                case 'l': lockFocusedOrPriority(); break;
                case 'n':
                    ctx.noiseFilter = !ctx.noiseFilter;
                    lastHudMs = 0;
                    break;
                case 'g':
                    ctx.geigerAudio = !ctx.geigerAudio;
                    lastHudMs = 0;
                    break;
                case 'b':
                    bruceConfig.setRadarBeep(!bruceConfig.radarBeep);
                    lastHudMs = 0;
                    break;
                case 'r':
                    portENTER_CRITICAL(&ctx.mux);
                    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) ctx.targets[i].active = false;
                    portEXIT_CRITICAL(&ctx.mux);
                    ctx.foxHistCount = 0;
                    ctx.hasFocused = false;
                    lastSweepEndMs = 0;
                    lastHudMs = 0;
                    break;
                case 'q': exitRadar = true; break;
                default: break;
            }
        }
#endif

        // 8) Hardware Navigation Buttons / Trackball
        if (check(EscPress)) {
            exitRadar = true;
        } else if (check(PrevPress)) {
#if defined(HAS_3_BUTTONS)
            if (view == RadarView::TABLE) stepSelection(-1);
            else cycleView(-1);
#else
            cycleView(-1);
#endif
        } else if (check(NextPress)) {
#if defined(HAS_3_BUTTONS)
            if (view == RadarView::TABLE) stepSelection(1);
            else cycleView(1);
#else
            cycleView(1);
#endif
        } else if (check(UpPress)) {
            if (view == RadarView::FOX_SPEC) cycleFilter(-1);
            else stepSelection(-1);
        } else if (check(DownPress)) {
            if (view == RadarView::FOX_SPEC) cycleFilter(1);
            else stepSelection(1);
        } else if (openMenuReq || check(SelPress)) {
            openMenuReq = false;
            stopAllScans();
#if defined(HAS_SPEAKER)
            if (isAudioPlaying()) stopAudioPlayback();
#endif
#if defined(HAS_TOUCH)
            touchZoneOutsideFooterEnabled = true;
#endif
            std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
            bool hasSelection = false;
            RadarTarget focused{};
            if (view == RadarView::THREAT) {
                std::vector<RadarTarget> alerts = extractAlertTargets(snapshotTargets(ctx, mode, true));
                if (!alerts.empty()) {
                    int idx = constrain(ctx.alertCursor, 0, (int)alerts.size() - 1);
                    focused = alerts[idx];
                    hasSelection = true;
                }
            } else if (!visible.empty()) {
                int cur = (view == RadarView::TABLE) ? ctx.tableCursor : ctx.scopeCursor;
                syncCursorToFocus(ctx, visible, cur);
                if (cur >= 0 && cur < (int)visible.size()) {
                    focused = visible[cur];
                    hasSelection = true;
                }
            }

            enum RadarMenuAction : uint8_t {
                ACT_NONE = 0,
                ACT_INSPECT,
                ACT_FOXHUNT,
                ACT_TRIANGULATE,
                ACT_AUTOLOCK,
                ACT_UNLOCK,
                ACT_GEIGER,
                ACT_RADAR_BEEP,
                ACT_CYCLE_VIEW,
                ACT_CYCLE_ZOOM,
                ACT_CYCLE_FILTER,
                ACT_NOISE_FILTER,
                ACT_LABELS,
                ACT_LOC_INFO,
                ACT_SET_GPS_ANCHOR,
                ACT_SAVE_LOC,
                ACT_EXPORT_CSV,
                ACT_RESCAN,
                ACT_EXIT,
            };
            RadarMenuAction act = ACT_NONE;

            char zoomMenuBuf[16];
            snprintf(zoomMenuBuf, sizeof(zoomMenuBuf), "Zoom: %.0fm", ctx.rangeMaxM);

            // Flat local menu vector: callbacks ONLY set `act`, so nested dialogs run after loopOptions returns!
            std::vector<Option> menuOpts;
            menuOpts.push_back({"Inspect Target", [&]() { act = ACT_INSPECT; }});
            menuOpts.push_back({"Fox-Hunt / Geiger", [&]() { act = ACT_FOXHUNT; }});
            menuOpts.push_back({"Locate / Triangulate", [&]() { act = ACT_TRIANGULATE; }});
            if (ctx.hasLocked) {
                menuOpts.push_back({"Unlock Target", [&]() { act = ACT_UNLOCK; }});
                menuOpts.push_back({ctx.geigerAudio ? "Geiger Beep: ON" : "Geiger Beep: OFF",
                                    [&]() { act = ACT_GEIGER; }});
            } else {
                menuOpts.push_back({"Lock", [&]() { act = ACT_AUTOLOCK; }});
            }
            menuOpts.push_back({String("View: ") + viewName(view, ctx.hasLocked),
                                [&]() { act = ACT_CYCLE_VIEW; }});
            menuOpts.push_back({String(zoomMenuBuf), [&]() { act = ACT_CYCLE_ZOOM; }});
            menuOpts.push_back({modeLabel(mode), [&]() { act = ACT_CYCLE_FILTER; }});
            menuOpts.push_back({ctx.noiseFilter ? "Noise Filter: ON" : "Noise Filter: OFF",
                                [&]() { act = ACT_NOISE_FILTER; }});
            menuOpts.push_back({ctx.showLabels ? "Labels: ON" : "Labels: OFF", [&]() { act = ACT_LABELS; }});
            menuOpts.push_back({bruceConfig.radarBeep ? "Radar Beep: ON" : "Radar Beep: OFF",
                                [&]() { act = ACT_RADAR_BEEP; }});
            menuOpts.push_back({"Location State", [&]() { act = ACT_LOC_INFO; }});
            menuOpts.push_back({"Set GPS Anchor", [&]() { act = ACT_SET_GPS_ANCHOR; }});
            menuOpts.push_back({"Save Location State", [&]() { act = ACT_SAVE_LOC; }});
            menuOpts.push_back({"Export CSV", [&]() { act = ACT_EXPORT_CSV; }});
            menuOpts.push_back({"Rescan Now", [&]() { act = ACT_RESCAN; }});
            menuOpts.push_back({"Exit", [&]() { act = ACT_EXIT; }});

            loopOptions(menuOpts, MENU_TYPE_SUBMENU, "Radar");

            switch (act) {
                case ACT_INSPECT: {
                    RadarTarget chosen = focused;
                    bool have = hasSelection || pickTarget(ctx, visible, "Inspect Target", chosen);
                    if (have) {
                        bool wantTri = false, wantFox = false;
                        showDeviceInspector(ctx, chosen, wantTri, wantFox);
                        if (wantTri) {
                            runTriangulateWizard(ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan);
                            if (bruceLocation.hasLocation()) bruceLocation.save();
                        } else if (wantFox) {
                            view = RadarView::FOX_SPEC;
                        }
                    }
                    break;
                }
                case ACT_FOXHUNT: {
                    RadarTarget chosen = focused;
                    bool have = hasSelection || pickTarget(ctx, visible, "Fox-Hunt / Geiger", chosen);
                    if (have) {
                        lockOnTarget(ctx, chosen);
                        view = RadarView::FOX_SPEC;
                    }
                    break;
                }
                case ACT_TRIANGULATE: {
                    RadarTarget chosen = focused;
                    bool have = hasSelection;
                    if (!have && ctx.hasLocked) {
                        for (const auto &v : visible) {
                            if (isSameTarget(v, ctx.lockedType, ctx.lockedMac)) {
                                chosen = v;
                                have = true;
                                break;
                            }
                        }
                    }
                    if (!have) have = pickTarget(ctx, visible, "Locate / Triangulate", chosen);
                    if (have) {
                        lockOnTarget(ctx, chosen);
                        runTriangulateWizard(ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan);
                        if (bruceLocation.hasLocation()) bruceLocation.save();
                    }
                    break;
                }
                case ACT_AUTOLOCK:
                    lockFocusedOrPriority();
                    break;
                case ACT_UNLOCK:
                    ctx.hasLocked = false;
                    break;
                case ACT_GEIGER:
                    ctx.geigerAudio = !ctx.geigerAudio;
                    break;
                case ACT_RADAR_BEEP:
                    bruceConfig.setRadarBeep(!bruceConfig.radarBeep);
                    break;
                case ACT_CYCLE_VIEW:
                    view = static_cast<RadarView>(((int)view + 1) % 4);
                    break;
                case ACT_CYCLE_ZOOM:
                    cycleZoom();
                    break;
                case ACT_CYCLE_FILTER:
                    mode = static_cast<RadarMode>(((int)mode + 1) % 5);
                    nextIsBle = (mode == RadarMode::BLE_ONLY);
                    ctx.scopeCursor = 0;
                    ctx.scopeScroll = 0;
                    ctx.tableCursor = 0;
                    ctx.tableScroll = 0;
                    ctx.hasFocused = false;
                    break;
                case ACT_NOISE_FILTER:
                    ctx.noiseFilter = !ctx.noiseFilter;
                    ctx.scopeCursor = 0;
                    ctx.scopeScroll = 0;
                    ctx.tableCursor = 0;
                    ctx.tableScroll = 0;
                    break;
                case ACT_LABELS:
                    ctx.showLabels = !ctx.showLabels;
                    break;
                case ACT_LOC_INFO:
                    bruceLocation.showLocationInfoScreen();
                    break;
                case ACT_SET_GPS_ANCHOR:
                    if (bruceLocation.promptManualGpsAnchor()) {
                        displaySuccess("GPS Anchor Saved", true);
                    }
                    break;
                case ACT_SAVE_LOC:
                    if (bruceLocation.save()) {
                        displaySuccess("Location State Saved", true);
                    } else {
                        displayError("No storage found", true);
                    }
                    break;
                case ACT_EXPORT_CSV:
                    exportRadarCsv(visible);
                    break;
                case ACT_RESCAN:
                    portENTER_CRITICAL(&ctx.mux);
                    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) ctx.targets[i].active = false;
                    portEXIT_CRITICAL(&ctx.mux);
                    ctx.foxHistCount = 0;
                    ctx.hasFocused = false;
                    lastSweepEndMs = 0;
                    break;
                case ACT_EXIT:
                    exitRadar = true;
                    break;
                default:
                    break;
            }

#if defined(HAS_TOUCH)
            touchZoneOutsideFooterEnabled = false;
            inputLock();
            touchPoint.Clear();
            inputUnlock();
#endif
            if (!exitRadar) {
                drawLayout();
                lastHudMs = 0;
                lastSweepEndMs = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    stopAllScans();
#if defined(HAS_SPEAKER)
    if (isAudioPlaying()) stopAudioPlayback();
#endif
#if !defined(BRUCE_COMPACT_CJK_FONT)
    if (scopeSprReady) scopeSpr.deleteSprite();
#endif
    if (radarScan) {
        radarScan->setScanCallbacks(nullptr);
        radarScan->clearResults();
    }
    if (bruceLocation.hasLocation() || bruceLocation.anchorCount() > 0) {
        bruceLocation.save();
    }
    if (ctx.gpsStarted) {
        Serial2.end();
        ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
    }
    s_radarCtx = nullptr;
    free(ctx.targets);
    check(SelPress);
    check(EscPress);
}

void radar_locate_setup() {
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
            uint8_t flags = 0;
            RadarCategory cat = RadarCategory::WIFI_SECURED;
            if (ssid.isEmpty()) cat = RadarCategory::WIFI_HIDDEN;
            else if (auth == WIFI_AUTH_OPEN) {
                cat = RadarCategory::WIFI_OPEN;
                flags |= RFLAG_WEAK_SEC;
            } else if (auth == WIFI_AUTH_WEP) {
                cat = RadarCategory::WIFI_WEP;
                flags |= RFLAG_WEAK_SEC;
            }
            radarUpsertTarget(
                &ctx,
                RadarTargetType::WIFI,
                cat,
                flags,
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
        detectEvilTwins(ctx);
    }

    std::vector<RadarTarget> found = snapshotTargets(ctx, RadarMode::BOTH);
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
