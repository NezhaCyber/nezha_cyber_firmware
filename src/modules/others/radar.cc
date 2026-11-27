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
constexpr uint32_t RADAR_STALE_MS = 30000;
constexpr uint32_t RADAR_BLE_SCAN_MS = 2100;
constexpr float RADAR_PI = 3.14159265f;
constexpr float RADAR_MAX_DIST_M = 30.0f;
constexpr float RADAR_TRIANGULATE_STEP_M = 2.0f;

// 哪吒魔童 × Military EW Tactical Radar Palette (哪吒1 Wind-Fire Gold & 哪吒2 Abyssal Sea-Flame)
constexpr uint16_t COL_GRID_DIM = 0x3041;      // Deep volcanic crimson-ember grid
constexpr uint16_t COL_GRID_MID = 0x60A2;      // Crimson-bronze range ring
constexpr uint16_t COL_SPOKE_DIM = 0x2000;     // Faint Wind-Fire Wheel diagonal spoke
constexpr uint16_t COL_QIANKUN_DIM = 0x6A60;   // Burnished Qiankun bronze outer bezel
constexpr uint16_t COL_QIANKUN_GOLD = 0xFEA0;  // Radiant Qiankun tactical gold
constexpr uint16_t COL_SWEEP_T4 = 0x1800;      // Deep ember afterglow
constexpr uint16_t COL_SWEEP_T3 = 0x3000;      // Ember fade far
constexpr uint16_t COL_SWEEP_T2 = 0x6840;      // Hun Tian Ling crimson mid trail
constexpr uint16_t COL_SWEEP_T1 = 0xC960;      // Blazing flame-orange near trail
constexpr uint16_t COL_SWEEP_LEAD = 0xFE80;    // Wind-Fire gold leading EW beam
constexpr uint16_t COL_N2_GRID_DIM = 0x0984;   // 哪吒2 deep abyssal sonar grid
constexpr uint16_t COL_N2_GRID_MID = 0x1327;   // 哪吒2 tactical steel-cyan ring
constexpr uint16_t COL_N2_SWEEP_T2 = 0x0B46;   // 哪吒2 sea-wave mid trail
constexpr uint16_t COL_N2_SWEEP_T1 = 0x15AE;   // 哪吒2 sea-surge near trail
constexpr uint16_t COL_N2_SWEEP_LEAD = 0x3F7F; // 哪吒2 blazing cyan-white EW beam
constexpr uint16_t COL_TAC_GREEN = 0x2F49;     // Military HUD nominal green
constexpr uint16_t COL_BLE_SAMSUNG = 0x4C5F;   // Sky blue
constexpr uint16_t COL_BLE_MS = 0x5EBF;        // Teal
constexpr uint16_t COL_ROW_SEL = 0x3861;       // Dark crimson-lacquer row highlight
constexpr uint16_t COL_HEADER_BG = 0x2041;     // Crimson-obsidian armored header strip
constexpr uint16_t COL_PANEL_BG = 0x1021;      // Stealth cockpit sub-panel fill

// Pentest threat/alert bitmask
constexpr uint8_t RFLAG_TRACKER = 0x01;   // AirTag / SmartTag / Tile / iBeacon / Eddystone
constexpr uint8_t RFLAG_EVIL_TWIN = 0x02; // Duplicate SSID across distinct BSSIDs
constexpr uint8_t RFLAG_HACK_DEV = 0x04;  // Flipper Zero / Pwnagotchi / Espressif dev
constexpr uint8_t RFLAG_WEAK_SEC = 0x08;  // Open / WEP Wi-Fi

enum class RadarMode : uint8_t { BOTH = 0, WIFI_ONLY = 1, BLE_ONLY = 2, ALERTS_ONLY = 3, OPEN_WIFI = 4 };
enum class RadarView : uint8_t { SCOPE = 0, TABLE = 1, FOX_SPEC = 2 };
enum class RadarTargetType : uint8_t { WIFI = 0, BLE = 1 };

enum class RadarCategory : uint8_t {
    WIFI_OPEN = 0,
    WIFI_SECURED = 1,
    WIFI_HIDDEN = 2,
    WIFI_WEP = 3,
    BLE_TRACKER = 4,
    BLE_HACK = 5, // Flipper Zero / Pwnagotchi
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

// Compact pentest-focused OUI vendor table in const flash (.rodata)
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
    {0x34, 0xD2, 0x62, "DJI"      }, {0x44, 0x19, 0xB6, "Hikvisn"  }, {0xBC, 0xAD, 0x28, "Hikvisn"  },
    {0x80, 0xE1, 0x26, "Flipper"  }, {0xDE, 0xAD, 0xBE, "Pwnagtch" }, {0x4C, 0xFC, 0xAA, "Tesla"    },
    {0x90, 0x84, 0x0D, "Apple"    }, {0x00, 0x17, 0x88, "Hue"      }, {0x38, 0xF7, 0x3D, "Amazon"   },
};

static const char *lookupOuiVendor(const uint8_t mac[6]) {
    for (size_t i = 0; i < sizeof(kOuiTable) / sizeof(kOuiTable[0]); ++i) {
        if (kOuiTable[i].b0 == mac[0] && kOuiTable[i].b1 == mac[1] && kOuiTable[i].b2 == mac[2]) {
            return kOuiTable[i].vendor;
        }
    }
    return nullptr;
}

struct RadarTarget {
    bool active = false;
    RadarTargetType type = RadarTargetType::WIFI;
    RadarCategory category = RadarCategory::BLE_GENERIC;
    uint8_t flags = 0;
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
    bool showLabels = true;
    bool geigerAudio = false;
    int tableCursor = 0;
    int tableScroll = 0;

    // Rolling RSSI sparkline history for Fox-Hunt mode
    int8_t foxHist[RADAR_HIST_LEN] = {0};
    size_t foxHistCount = 0;
    int8_t foxPeakRssi = -100;
    uint32_t lastGeigerMs = 0;

    // Optional live GPS state
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

static uint16_t categoryColor(RadarCategory cat, uint8_t flags = 0) {
    if (flags & RFLAG_EVIL_TWIN) return TFT_RED;
    switch (cat) {
        case RadarCategory::WIFI_OPEN: return TFT_GREEN;
        case RadarCategory::WIFI_WEP: return TFT_RED;
        case RadarCategory::WIFI_SECURED: return TFT_CYAN;
        case RadarCategory::WIFI_HIDDEN: return TFT_YELLOW;
        case RadarCategory::BLE_TRACKER: return TFT_RED;
        case RadarCategory::BLE_HACK: return TFT_ORANGE;
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

static void drawCategoryGlyph(int x, int y, RadarCategory cat, uint16_t col, uint8_t flags = 0) {
    if (flags & RFLAG_EVIL_TWIN) {
        tft.drawRect(x - 4, y - 4, 9, 9, TFT_RED);
        tft.fillRect(x - 2, y - 2, 5, 5, TFT_YELLOW);
        return;
    }
    switch (cat) {
        case RadarCategory::WIFI_OPEN:
        case RadarCategory::WIFI_WEP:
        case RadarCategory::WIFI_SECURED:
            tft.fillRect(x - 2, y - 2, 5, 5, col);
            break;
        case RadarCategory::WIFI_HIDDEN:
            tft.fillTriangle(x, y - 3, x - 3, y + 2, x + 3, y + 2, col);
            break;
        case RadarCategory::BLE_TRACKER:
        case RadarCategory::BLE_HACK:
            tft.drawCircle(x, y, 4, (cat == RadarCategory::BLE_HACK) ? TFT_YELLOW : TFT_ORANGE);
            tft.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
            tft.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
            break;
        case RadarCategory::BLE_APPLE:
            tft.fillTriangle(x, y - 3, x - 3, y, x + 3, y, col);
            tft.fillTriangle(x, y + 3, x - 3, y, x + 3, y, col);
            break;
        default:
            tft.fillCircle(x, y, 2, col);
            break;
    }
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

    // Check known RF/pentest hardware by name or OUI (Flipper Zero, Pwnagotchi, Espressif)
    if (!nm.empty()) {
        if (nm.find("Flipper") != std::string::npos) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Flipper", subtypeSz - 1);
        } else if (nm.find("pwn") != std::string::npos || nm.find("Pwn") != std::string::npos) {
            cat = RadarCategory::BLE_HACK;
            flags |= RFLAG_HACK_DEV;
            strncpy(subtypeBuf, "Pwnagotchi", subtypeSz - 1);
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
        } else if (!subtypeBuf[0]) {
            strncpy(subtypeBuf, oui, subtypeSz - 1);
        }
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
}

// Flag Wi-Fi Evil-Twin / duplicate SSID anomalies (same SSID with mismatched encryption!)
static void detectEvilTwins(RadarContext &ctx) {
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
                }
            }
        }
    }
    portEXIT_CRITICAL(&ctx.mux);
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

static int distToRadius(float distM, int maxR) {
    float norm = log10f(1.0f + constrain(distM, 0.2f, RADAR_MAX_DIST_M)) / log10f(1.0f + RADAR_MAX_DIST_M);
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
        case RadarMode::WIFI_ONLY: return "WIFI";
        case RadarMode::BLE_ONLY: return "BLE";
        case RadarMode::ALERTS_ONLY: return "ALRT";
        case RadarMode::OPEN_WIFI: return "OPEN";
        default: return "W+B";
    }
}

static const char *viewName(RadarView v, bool hasLocked) {
    switch (v) {
        case RadarView::TABLE: return "Intel Table";
        case RadarView::FOX_SPEC: return hasLocked ? "Fox-Hunt" : "2.4G Spectrum";
        default: return "Tactical Radar";
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

static std::vector<RadarTarget> snapshotTargets(RadarContext &ctx, RadarMode mode, bool ignoreFilter = false) {
    std::vector<RadarTarget> list;
    portENTER_CRITICAL(&ctx.mux);
    for (size_t i = 0; i < RADAR_MAX_TARGETS; ++i) {
        const RadarTarget &t = ctx.targets[i];
        if (!t.active) continue;
        if (!ignoreFilter) {
            if (mode == RadarMode::WIFI_ONLY && t.type != RadarTargetType::WIFI) continue;
            if (mode == RadarMode::BLE_ONLY && t.type != RadarTargetType::BLE) continue;
            if (mode == RadarMode::ALERTS_ONLY && (t.flags & (RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) == 0)
                continue;
            if (mode == RadarMode::OPEN_WIFI &&
                !(t.type == RadarTargetType::WIFI &&
                  (t.category == RadarCategory::WIFI_OPEN || t.category == RadarCategory::WIFI_WEP)))
                continue;
        }
        list.push_back(t);
    }
    portEXIT_CRITICAL(&ctx.mux);
    std::sort(list.begin(), list.end(), [](const RadarTarget &a, const RadarTarget &b) {
        return a.distM < b.distM;
    });
    return list;
}

static void lockOnTarget(RadarContext &ctx, const RadarTarget &t) {
    ctx.hasLocked = true;
    ctx.lockedType = t.type;
    memcpy(ctx.lockedMac, t.mac, 6);
    ctx.foxHistCount = 0;
    ctx.foxPeakRssi = t.rssi;
}

// =====================================================================
// VIEW 0: MILITARY-GRADE EW TACTICAL PPI RADAR SCOPE × 哪吒魔童
// =====================================================================
static void drawRadarScope(
    int cx, int cy, int r, float sweepRad, const std::vector<RadarTarget> &list, const RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    const bool isNezha2 = (bruceConfig.nezhaThemeStyle >= 4);
    const uint16_t gridDim = isNezha2 ? COL_N2_GRID_DIM : COL_GRID_DIM;
    const uint16_t gridMid = isNezha2 ? COL_N2_GRID_MID : COL_GRID_MID;
    const uint16_t bezelCol = isNezha2 ? COL_N2_SWEEP_T1 : COL_QIANKUN_DIM;
    const uint16_t accentGold = isNezha2 ? COL_N2_SWEEP_LEAD : COL_QIANKUN_GOLD;

    // Clear scope square viewport and frame with military corner acquisition brackets
    tft.fillRect(cx - r - 4, cy - r - 4, (r + 4) * 2 + 1, (r + 4) * 2 + 1, bg);
    drawNezhaCornerBrackets(cx - r - 3, cy - r - 3, (r + 3) * 2 + 1, (r + 3) * 2 + 1, bruceConfig.priColor, accentGold);

    // Calibrated range rings (2m, 8m, 30m) + 45-deg Wind-Fire radials + stadiametric crosshairs
    int r2 = distToRadius(2.0f, r);
    int r8 = distToRadius(8.0f, r);
    int diag = (int)(r * 0.7071f);
    tft.drawLine(cx - diag, cy - diag, cx + diag, cy + diag, COL_SPOKE_DIM);
    tft.drawLine(cx - diag, cy + diag, cx + diag, cy - diag, COL_SPOKE_DIM);
    tft.drawFastHLine(cx - r, cy, 2 * r + 1, gridDim);
    tft.drawFastVLine(cx, cy - r, 2 * r + 1, gridDim);

    // Stadiametric range hash ticks along cardinal axes (military gunsight reticle)
    for (int s = 1; s <= 3; ++s) {
        int off = (r * s) / 4;
        tft.drawFastVLine(cx - off, cy - 2, 5, gridMid);
        tft.drawFastVLine(cx + off, cy - 2, 5, gridMid);
        tft.drawFastHLine(cx - 2, cy - off, 5, gridMid);
        tft.drawFastHLine(cx - 2, cy + off, 5, gridMid);
    }

    // Double-ring armored Qiankun / EW bezel + inner range rings
    tft.drawCircle(cx, cy, r, bezelCol);
    if (r >= 24) tft.drawCircle(cx, cy, r - 2, gridMid);
    tft.drawCircle(cx, cy, r8, gridDim);
    tft.drawCircle(cx, cy, r2, gridDim);

    // Mil-spec compass bezel ticks every 10 deg (bold 90 deg, medium 30 deg, fine 10 deg)
    for (int deg = 0; deg < 360; deg += 10) {
        bool card = (deg % 90 == 0);
        bool major = (deg % 30 == 0);
        if (!major && r < 46) continue;
        float a = (float)deg * (RADAR_PI / 180.0f);
        int tickLen = card ? 7 : (major ? 4 : 2);
        int x1 = cx + (int)(cosf(a) * r);
        int y1 = cy + (int)(sinf(a) * r);
        int x2 = cx + (int)(cosf(a) * (r - tickLen));
        int y2 = cy + (int)(sinf(a) * (r - tickLen));
        tft.drawLine(x1, y1, x2, y2, card ? accentGold : (major ? gridMid : gridDim));
    }

    // Military 3-digit azimuth bearings (000 / 090 / 180 / 270) + range markers
    if (r >= 48) {
        tft.setTextSize(1);
        tft.setTextColor(accentGold, bg);
        tft.drawCentreString("000", cx, cy - r + 8, 1);
        tft.drawRightString("090", cx + r - 8, cy - 3, 1);
        tft.drawCentreString("180", cx, cy + r - 15, 1);
        tft.drawString("270", cx - r + 8, cy - 3, 1);
        tft.setTextColor(gridMid, bg);
        tft.drawString("2m", cx + 3, cy - r2 + 1, 1);
        tft.drawString("8m", cx + 3, cy - r8 + 1, 1);

        // Live sweep azimuth readout tucked cleanly in bottom-left corner of scope
        int azDeg = ((int)roundf(sweepRad * (180.0f / RADAR_PI)) + 90) % 360;
        if (azDeg < 0) azDeg += 360;
        char azBuf[10];
        snprintf(azBuf, sizeof(azBuf), "AZ%03d", azDeg);
        tft.setTextColor(COL_QIANKUN_GOLD, bg);
        tft.drawString(azBuf, cx - r - 1, cy + r - 6, 1);
    }

    // 5-ray Military Phased-Array / Wind-Fire Wheel sweep wedge + hot emitter dot on outer rim
    const float trailOff[4] = {0.42f, 0.30f, 0.19f, 0.09f};
    const uint16_t trailCol[4] = {
        COL_SWEEP_T4,
        isNezha2 ? COL_N2_GRID_DIM : COL_SWEEP_T3,
        isNezha2 ? COL_N2_SWEEP_T2 : COL_SWEEP_T2,
        isNezha2 ? COL_N2_SWEEP_T1 : COL_SWEEP_T1,
    };
    for (int t = 0; t < 4; ++t) {
        float ang = sweepRad - trailOff[t];
        tft.drawLine(cx, cy, cx + (int)(cosf(ang) * (r - 3)), cy + (int)(sinf(ang) * (r - 3)), trailCol[t]);
    }
    int leadX = cx + (int)(cosf(sweepRad) * (r - 3));
    int leadY = cy + (int)(sinf(sweepRad) * (r - 3));
    tft.drawLine(cx, cy, leadX, leadY, isNezha2 ? COL_N2_SWEEP_LEAD : COL_SWEEP_LEAD);
    tft.fillCircle(leadX, leadY, 2, bruceConfig.priColor);
    tft.drawPixel(leadX, leadY, TFT_WHITE);

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
        int lx = (bx + 6 + tw <= cx + r) ? (bx + 6) : (bx - tw - 5);
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

    for (int i = (int)list.size() - 1; i >= 0; --i) {
        const RadarTarget &t = list[i];
        int distPx = distToRadius(t.distM, r);
        float drawAngle = t.hasTriangulated ? atan2f(-t.relY, t.relX) : t.angleRad;
        int bx = cx + (int)(cosf(drawAngle) * distPx);
        int by = cy + (int)(sinf(drawAngle) * distPx);

        bool locked = ctx.hasLocked && t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0;
        uint16_t col = categoryColor(t.category, t.flags);
        if (locked) {
            // Fighter-jet square target acquisition reticle + bearing vector line
            tft.drawLine(cx, cy, bx, by, COL_QIANKUN_GOLD);
            const int b = 6;
            tft.drawFastHLine(bx - b, by - b, 4, TFT_RED);
            tft.drawFastVLine(bx - b, by - b, 4, TFT_RED);
            tft.drawFastHLine(bx + b - 3, by - b, 4, TFT_RED);
            tft.drawFastVLine(bx + b, by - b, 4, TFT_RED);
            tft.drawFastHLine(bx - b, by + b, 4, TFT_RED);
            tft.drawFastVLine(bx - b, by + b - 3, 4, TFT_RED);
            tft.drawFastHLine(bx + b - 3, by + b, 4, TFT_RED);
            tft.drawFastVLine(bx + b, by + b - 3, 4, TFT_RED);
            tft.drawRect(bx - 3, by - 3, 7, 7, COL_QIANKUN_GOLD);
            tft.fillCircle(bx, by, 1, TFT_WHITE);
            tryDrawCallout(bx, by, t, COL_QIANKUN_GOLD, true);
        } else {
            drawCategoryGlyph(bx, by, t.category, col, t.flags);
            if (i < 4 || (t.flags & (RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV))) {
                tryDrawCallout(bx, by, t, col, (t.flags & (RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) != 0);
            }
        }
    }

    // Center tactical bore-sight ring + 哪吒魔童 crest (auto-matches 哪吒1 / 哪吒2 style!)
    tft.drawCircle(cx, cy, (r >= 48) ? 7 : 5, gridMid);
    drawNezhaCrest(cx, cy, (r >= 48) ? 4 : 3, bruceConfig.priColor, accentGold, max(1, bruceConfig.nezhaThemeStyle));
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

    int wifiCnt = 0, bleCnt = 0, alertCnt = 0, weakCnt = 0;
    const RadarTarget *lockedTarget = nullptr;
    float anchorSumX = 0.0f, anchorSumY = 0.0f;
    int anchorCnt = 0;

    for (const auto &t : list) {
        if (t.type == RadarTargetType::WIFI) ++wifiCnt;
        else {
            ++bleCnt;
            if (anchorCnt < 6) {
                float w = 1.0f / max(0.5f, t.distM);
                anchorSumX += t.relX * w;
                anchorSumY += t.relY * w;
                ++anchorCnt;
            }
        }
        if (t.flags & (RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) ++alertCnt;
        if (t.flags & RFLAG_WEAK_SEC) ++weakCnt;
        if (ctx.hasLocked && t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0) {
            lockedTarget = &t;
        }
    }

    int y = hudY;
    // Armored Military EW Callsign Strip + DEFCON Threat Pill
    tft.fillRect(hudX, y, hudW, LH + 4, COL_HEADER_BG);
    drawNezhaCrest(hudX + 5, y + (LH + 4) / 2, 3, bruceConfig.priColor, COL_QIANKUN_GOLD, max(1, bruceConfig.nezhaThemeStyle));
    tft.setTextColor(COL_QIANKUN_GOLD, COL_HEADER_BG);
    String radioBadge = scanningWifi ? "W*" : (scanningBle ? "B*" : "..");
    String hdr = String(modeShort(mode)) + ":" + radioBadge + (timeSliced ? "/T" : "/C");
    tft.drawString(hdr, hudX + 11, y + 2, 1);

    const char *thrtLbl = (alertCnt > 0) ? "CRIT" : ((weakCnt > 0) ? "WARN" : "NORM");
    uint16_t thrtCol = (alertCnt > 0) ? TFT_RED : ((weakCnt > 0) ? COL_QIANKUN_GOLD : COL_TAC_GREEN);
    tft.fillRect(hudX + hudW - 28, y + 1, 27, LH + 2, thrtCol);
    tft.setTextColor(uiTextOnAccentColor(thrtCol), thrtCol);
    tft.drawCentreString(thrtLbl, hudX + hudW - 14, y + 2, 1);
    y += LH + 6;

    // Live Military Grid / GPS / RF-Anchor Telemetry
    char locLine[36];
    if (bruceLocation.hasLocation()) {
        uint16_t locCol = bruceLocation.hasLiveGps()
                              ? COL_TAC_GREEN
                              : ((bruceLocation.source() == "RF-FIX") ? TFT_CYAN : COL_QIANKUN_GOLD);
        tft.setTextColor(locCol, bg);
        snprintf(
            locLine,
            sizeof(locLine),
            "%s %.3f,%.3f",
            bruceLocation.source().substring(0, 3).c_str(),
            bruceLocation.lat(),
            bruceLocation.lon()
        );
    } else if (anchorCnt > 0) {
        tft.setTextColor(TFT_LIGHTGREY, bg);
        snprintf(
            locLine,
            sizeof(locLine),
            "VEC:%d %+.1f,%+.1fm",
            anchorCnt,
            anchorSumX / anchorCnt,
            anchorSumY / anchorCnt
        );
    } else {
        tft.setTextColor(TFT_DARKGREY, bg);
        snprintf(locLine, sizeof(locLine), "GRID: ACQUIRING...");
    }
    tft.drawString(String(locLine).substring(0, maxChars), hudX, y, 1);
    y += LH + 2;

    // Tabular EW Contact Counter Strip (AP / BT / AL)
    tft.fillRect(hudX, y, hudW, LH + 3, COL_PANEL_BG);
    tft.fillRect(hudX + 2, y + 3, 4, 4, TFT_CYAN);
    tft.setTextColor(TFT_CYAN, COL_PANEL_BG);
    tft.drawString("AP:" + String(wifiCnt), hudX + 8, y + 1, 1);

    int col2 = hudX + hudW / 3 + 2;
    tft.fillCircle(col2 + 2, y + 5, 2, TFT_MAGENTA);
    tft.setTextColor(TFT_MAGENTA, COL_PANEL_BG);
    tft.drawString("BT:" + String(bleCnt), col2 + 6, y + 1, 1);

    if (hudW >= 116) {
        int col3 = hudX + (hudW * 2) / 3 + 2;
        tft.fillTriangle(col3 + 2, y + 1, col3, y + 4, col3 + 4, y + 4, alertCnt > 0 ? TFT_RED : TFT_DARKGREY);
        tft.fillTriangle(col3 + 2, y + 8, col3, y + 4, col3 + 4, y + 4, alertCnt > 0 ? TFT_RED : TFT_DARKGREY);
        tft.setTextColor(alertCnt > 0 ? TFT_RED : TFT_DARKGREY, COL_PANEL_BG);
        tft.drawString("AL:" + String(alertCnt), col3 + 6, y + 1, 1);
    }
    y += LH + 5;

    drawNezhaRibbonRule(hudX, y, hudW);
    y += 4;

    if (ctx.hasLocked) {
        int lockBoxH = max(40, hudY + hudH - y - 1);
        tft.fillRect(hudX, y, hudW, lockBoxH, COL_PANEL_BG);
        drawNezhaCornerBrackets(hudX, y, hudW, lockBoxH, TFT_RED, COL_QIANKUN_GOLD);
        int ly = y + 4;
        int lx = hudX + 5;
        int lw = hudW - 10;

        tft.setTextColor(COL_QIANKUN_GOLD, COL_PANEL_BG);
        tft.drawString("TGT LOCK", lx, ly, 1);
        ly += LH + 2;

        if (lockedTarget) {
            uint16_t catCol = categoryColor(lockedTarget->category, lockedTarget->flags);
            drawCategoryGlyph(lx + 3, ly + 3, lockedTarget->category, catCol, lockedTarget->flags);
            tft.setTextColor(catCol, COL_PANEL_BG);
            String catTag = bruce_i18n::tr(categoryName(lockedTarget->category));
            catTag = bruce_i18n::utf8TruncateToWidth(catTag, lw - 10, FP);
            tft.drawString(catTag, lx + 9, ly, 1);
            ly += lineStep;

            String label = targetDisplayName(*lockedTarget);
            tft.setTextColor(TFT_WHITE, COL_PANEL_BG);
            tft.drawString(label.substring(0, max(5, lw / LW)), lx, ly, 1);
            ly += LH + 2;

            int pct = constrain((lockedTarget->rssi + 95) * 100 / 60, 0, 100);
            uint16_t barCol = (pct > 66) ? COL_TAC_GREEN : ((pct > 33) ? COL_QIANKUN_GOLD : TFT_RED);
            char trend = (lockedTarget->rssi > lockedTarget->prevRssi + 1)
                             ? '+'
                             : ((lockedTarget->rssi < lockedTarget->prevRssi - 1) ? '-' : '=');
            tft.setTextColor(barCol, COL_PANEL_BG);
            tft.drawString(
                formatDistShort(lockedTarget->distM) + " " + String(lockedTarget->rssi) + "dB " + trend,
                lx,
                ly,
                1
            );
            ly += LH + 2;

            char coordBuf[32];
            if (lockedTarget->hasTriangulated) {
                float brg = vectorToBearingDeg(lockedTarget->relX, lockedTarget->relY);
                snprintf(
                    coordBuf,
                    sizeof(coordBuf),
                    "BRG:%03d %s %+.0f,%+.0f",
                    (int)roundf(brg),
                    bearingToCardinal(brg),
                    lockedTarget->relX,
                    lockedTarget->relY
                );
            } else {
                snprintf(
                    coordBuf,
                    sizeof(coordBuf),
                    "PK:%d %s",
                    (int)lockedTarget->peakRssi,
                    formatDwell(lockedTarget->firstSeenMs, millis()).c_str()
                );
            }
            tft.setTextColor(TFT_LIGHTGREY, COL_PANEL_BG);
            tft.drawString(String(coordBuf).substring(0, max(5, lw / LW)), lx, ly, 1);
            ly += LH + 3;

            // Segmented 10-cell military signal strength bar
            if (ly + 10 <= y + lockBoxH - 3) {
                const int segs = 10;
                const int segGap = 2;
                int segW = max(3, (lw - segGap * (segs - 1)) / segs);
                int lit = (pct * segs + 50) / 100;
                for (int s = 0; s < segs; ++s) {
                    uint16_t sc = (s < lit) ? ((s >= 7) ? COL_TAC_GREEN : ((s >= 4) ? COL_QIANKUN_GOLD : TFT_RED))
                                            : COL_GRID_DIM;
                    tft.fillRect(lx + s * (segW + segGap), ly, segW, 7, sc);
                }
            }
        } else {
            tft.setTextColor(TFT_RED, COL_PANEL_BG);
            tft.drawString(bruce_i18n::tr("Lost signal..."), lx, ly, 1);
        }
        return;
    }

    // Closest tactical contacts feed (numbered IDs + MIL-STD glyph + range)
    int rowH = LH + 2;
    int maxRows = max(1, (hudY + hudH - y - 2) / rowH);
    for (int i = 0; i < (int)list.size() && i < maxRows; ++i) {
        const RadarTarget &t = list[i];
        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyph(hudX + 3, y + 3, t.category, col, t.flags);

        String distStr = formatDistShort(t.distM);
        char trend = (t.rssi > t.prevRssi + 2) ? '+' : ((t.rssi < t.prevRssi - 2) ? '-' : ' ');
        if (trend != ' ') distStr = String(trend) + distStr;
        int nameBudget = max(3, maxChars - (int)distStr.length() - 3);
        String nm = targetDisplayName(t);
        if ((int)nm.length() > nameBudget) nm = nm.substring(0, nameBudget);

        tft.setTextColor(col, bg);
        tft.drawString(nm, hudX + 9, y, 1);
        tft.setTextColor(COL_QIANKUN_GOLD, bg);
        tft.drawRightString(distStr, hudX + hudW - 1, y, 1);
        y += rowH;
    }
}

// =====================================================================
// VIEW 1: LIVE TARGET INTELLIGENCE TABLE (AIRODUMP / KISMET STYLE)
// =====================================================================
static void drawIntelTable(
    int x, int y, int w, int h, RadarMode mode, bool timeSliced, bool scanningWifi, bool scanningBle,
    const std::vector<RadarTarget> &list, RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(x, y, w, h, bg);
    tft.setTextSize(1);

    int total = (int)list.size();
    if (total == 0) {
        ctx.tableCursor = 0;
        ctx.tableScroll = 0;
    } else {
        ctx.tableCursor = constrain(ctx.tableCursor, 0, total - 1);
    }

    // Top summary bar
    int wifiCnt = 0, bleCnt = 0, alertCnt = 0;
    for (const auto &t : list) {
        if (t.type == RadarTargetType::WIFI) ++wifiCnt;
        else ++bleCnt;
        if (t.flags & (RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) ++alertCnt;
    }

    // Armored SIGINT top summary bar + 哪吒 crest
    tft.fillRect(x, y, w, LH + 4, COL_PANEL_BG);
    drawNezhaCrest(x + 5, y + (LH + 4) / 2, 3, bruceConfig.priColor, COL_QIANKUN_GOLD, max(1, bruceConfig.nezhaThemeStyle));
    tft.setTextColor(COL_QIANKUN_GOLD, COL_PANEL_BG);
    String topInfo = "SIGINT " + String(modeShort(mode)) + (scanningWifi ? " [W*]" : (scanningBle ? " [B*]" : " [..]")) +
                     " AP:" + String(wifiCnt) + " BT:" + String(bleCnt);
    tft.drawString(topInfo, x + 12, y + 2, 1);
    if (alertCnt > 0) {
        tft.fillRect(x + w - 42, y + 1, 41, LH + 2, TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawCentreString("ALRT:" + String(alertCnt), x + w - 21, y + 2, 1);
    }
    int curY = y + LH + 5;
    drawNezhaRibbonRule(x, curY, w);
    curY += 2;

    // Column headers (crimson-obsidian lacquer strip + Qiankun gold military labels)
    tft.fillRect(x, curY, w, LH + 2, COL_HEADER_BG);
    tft.setTextColor(COL_QIANKUN_GOLD, COL_HEADER_BG);
    int colName = x + 10;
    int colTag = x + (w * 48) / 100;
    int colRssi = x + (w * 73) / 100;
    int colDist = x + w - 2;
    tft.drawString("CALLSIGN", colName, curY + 1, 1);
    if (w >= 210) tft.drawString("CH/SIG", colTag, curY + 1, 1);
    tft.drawString("dBm", colRssi, curY + 1, 1);
    tft.drawRightString("RNG", colDist, curY + 1, 1);
    curY += LH + 3;

    if (total == 0) {
        tft.setTextColor(TFT_DARKGREY, bg);
        tft.drawCentreString(bruce_i18n::tr("Scanning nearby BLE & WiFi..."), x + w / 2, curY + 20, 1);
        return;
    }

    const int rowH = LH + 4;
    const int visRows = max(1, (y + h - curY) / rowH);
    if (ctx.tableCursor < ctx.tableScroll) ctx.tableScroll = ctx.tableCursor;
    if (ctx.tableCursor >= ctx.tableScroll + visRows) ctx.tableScroll = ctx.tableCursor - visRows + 1;

    uint32_t now = millis();
    int nameChars = max(6, ((w >= 210 ? colTag : colRssi) - colName - 4) / LW);
    int tagChars = max(4, (colRssi - colTag - 4) / LW);

    for (int r = 0; r < visRows && (ctx.tableScroll + r) < total; ++r) {
        int idx = ctx.tableScroll + r;
        const RadarTarget &t = list[idx];
        bool sel = (idx == ctx.tableCursor);
        bool locked = ctx.hasLocked && t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0;
        uint16_t rowBg = sel ? COL_ROW_SEL : bg;
        if (sel) {
            tft.fillRect(x, curY, w, rowH - 1, rowBg);
            tft.fillRect(x, curY, 2, rowH - 1, COL_QIANKUN_GOLD);
        }

        uint16_t col = categoryColor(t.category, t.flags);
        drawCategoryGlyph(x + 4, curY + 4, t.category, col, t.flags);

        String nm = (locked ? "*" : "") + targetDisplayName(t);
        if ((int)nm.length() > nameChars) nm = nm.substring(0, nameChars);
        tft.setTextColor(locked ? TFT_YELLOW : col, rowBg);
        tft.drawString(nm, colName, curY + 1, 1);

        if (w >= 210) {
            String tg = targetShortTag(t);
            if ((int)tg.length() > tagChars) tg = tg.substring(0, tagChars);
            tft.setTextColor((t.flags & (RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) ? TFT_RED : TFT_LIGHTGREY, rowBg);
            tft.drawString(tg, colTag, curY + 1, 1);
        }

        int pct = constrain((t.rssi + 95) * 100 / 60, 0, 100);
        uint16_t rssiCol = (pct > 66) ? TFT_GREEN : ((pct > 33) ? TFT_YELLOW : TFT_RED);
        tft.setTextColor(rssiCol, rowBg);
        tft.drawString(String(t.rssi), colRssi, curY + 1, 1);

        tft.setTextColor(TFT_WHITE, rowBg);
        String distCell = formatDistShort(t.distM);
        if (w >= 280) distCell += " " + formatDwell(t.firstSeenMs, now);
        tft.drawRightString(distCell, colDist, curY + 1, 1);

        curY += rowH;
    }
}

// =====================================================================
// VIEW 2: RF FOX-HUNT / GEIGER TRACKER (LOCKED) OR 2.4G SPECTRUM (UNLOCKED)
// =====================================================================
static void drawFoxHuntOrSpectrum(
    int x, int y, int w, int h, const std::vector<RadarTarget> &allList, RadarContext &ctx
) {
    const uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(x, y, w, h, bg);

    if (ctx.hasLocked) {
        // ---- RF FOX-HUNT / GEIGER MODE ----
        const RadarTarget *lt = nullptr;
        for (const auto &t : allList) {
            if (t.type == ctx.lockedType && memcmp(t.mac, ctx.lockedMac, 6) == 0) {
                lt = &t;
                break;
            }
        }

        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW, bg);
        String title = lt ? (targetDisplayName(*lt) + " [" + targetShortTag(*lt) + "]")
                          : (formatMac(ctx.lockedMac) + " (LOST)");
        tft.drawString(title.substring(0, max(10, w / LW)), x + 2, y, 1);

        tft.setTextColor(ctx.geigerAudio ? TFT_GREEN : TFT_DARKGREY, bg);
        tft.drawRightString(ctx.geigerAudio ? "BEEP:ON" : "BEEP:OFF", x + w - 2, y, 1);

        int curY = y + LH + 3;
        if (lt) {
            int pct = constrain((lt->rssi + 95) * 100 / 60, 0, 100);
            uint16_t sigCol = (pct > 66) ? TFT_GREEN : ((pct > 33) ? TFT_YELLOW : TFT_RED);

            // Big RSSI & Distance readout
            tft.setTextSize(FM);
            tft.setTextColor(sigCol, bg);
            tft.drawString(String(lt->rssi) + " dBm", x + 2, curY, 1);
            tft.setTextColor(TFT_WHITE, bg);
            tft.drawRightString(formatDistShort(lt->distM), x + w - 2, curY, 1);
            curY += FM * LH + 3;

            // Peak / Dwell / Packets telemetry line
            tft.setTextSize(1);
            char meta[56];
            snprintf(
                meta,
                sizeof(meta),
                "PEAK:%ddBm  PKTS:%u  DWELL:%s",
                (int)ctx.foxPeakRssi,
                (unsigned)lt->seenCount,
                formatDwell(lt->firstSeenMs, millis()).c_str()
            );
            tft.setTextColor(TFT_CYAN, bg);
            tft.drawString(String(meta).substring(0, max(10, w / LW)), x + 2, curY, 1);
            curY += LH + 3;

            // Segmented 16-cell military proximity meter bar
            int barH = 10;
            tft.drawRect(x + 2, curY, w - 4, barH, COL_GRID_MID);
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

        // Rolling RSSI Sparkline Graph (sweep body/antenna 360 deg to find peak!)
        int gx = x + 2;
        int gy = curY;
        int gw = w - 4;
        int gh = (y + h) - gy - 2;
        if (gh >= 24) {
            tft.drawRect(gx, gy, gw, gh, COL_GRID_MID);
            // Reference lines at -50, -70, -90 dBm
            tft.drawFastHLine(gx + 1, gy + gh / 3, gw - 2, COL_GRID_DIM);
            tft.drawFastHLine(gx + 1, gy + (gh * 2) / 3, gw - 2, COL_GRID_DIM);

            if (ctx.foxHistCount >= 2) {
                int prevPx = -1, prevPy = -1;
                for (size_t i = 0; i < ctx.foxHistCount; ++i) {
                    int px = gx + 2 + (int)((i * (gw - 4)) / (RADAR_HIST_LEN - 1));
                    int clamped = constrain((int)ctx.foxHist[i], -98, -32);
                    int py = gy + gh - 2 - ((clamped + 98) * (gh - 4)) / 66;
                    uint16_t ptCol = (clamped > -58) ? TFT_GREEN : ((clamped > -75) ? TFT_YELLOW : TFT_RED);
                    if (prevPx >= 0) tft.drawLine(prevPx, prevPy, px, py, ptCol);
                    prevPx = px;
                    prevPy = py;
                }
            }
            tft.setTextSize(1);
            tft.setTextColor(TFT_DARKGREY, bg);
            tft.drawString("-35", gx + 2, gy + 2, 1);
            tft.drawString("-95", gx + 2, gy + gh - LH - 1, 1);
        }
        return;
    }

    // ---- 2.4 GHz CHANNEL CONGESTION & THREAT MONITOR ----
    int chCount[14] = {0};
    int chPeak[14];
    for (int c = 0; c < 14; ++c) chPeak[c] = -100;

    int openCnt = 0, wepCnt = 0, hiddenCnt = 0, twinCnt = 0, trackerCnt = 0, hackCnt = 0;
    for (const auto &t : allList) {
        if (t.type == RadarTargetType::WIFI) {
            if (t.channel >= 1 && t.channel <= 13) {
                chCount[t.channel]++;
                if (t.rssi > chPeak[t.channel]) chPeak[t.channel] = t.rssi;
            }
            if (t.category == RadarCategory::WIFI_OPEN) ++openCnt;
            if (t.category == RadarCategory::WIFI_WEP) ++wepCnt;
            if (t.category == RadarCategory::WIFI_HIDDEN) ++hiddenCnt;
            if (t.flags & RFLAG_EVIL_TWIN) ++twinCnt;
        } else {
            if (t.flags & RFLAG_TRACKER) ++trackerCnt;
            if (t.flags & RFLAG_HACK_DEV) ++hackCnt;
        }
    }

    int bestCh = 1, busyCh = 1;
    for (int c = 1; c <= 13; ++c) {
        if (chCount[c] < chCount[bestCh]) bestCh = c;
        if (chCount[c] > chCount[busyCh]) busyCh = c;
    }

    tft.fillRect(x, y, w, LH + 4, COL_HEADER_BG);
    drawNezhaCrest(x + 5, y + (LH + 4) / 2, 3, bruceConfig.priColor, COL_QIANKUN_GOLD, max(1, bruceConfig.nezhaThemeStyle));
    tft.setTextSize(1);
    tft.setTextColor(COL_QIANKUN_GOLD, COL_HEADER_BG);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "EW 2.4G  CLR:C%d  BUSY:C%d(%d)", bestCh, busyCh, chCount[busyCh]);
    tft.drawString(String(hdr).substring(0, max(10, (w - 14) / LW)), x + 12, y + 2, 1);
    drawNezhaRibbonRule(x, y + LH + 4, w);

    // Threat summary strip
    int curY = y + LH + 7;
    char threat[56];
    snprintf(
        threat,
        sizeof(threat),
        "OPN:%d WEP:%d HID:%d TWIN:%d TRK:%d RF:%d",
        openCnt,
        wepCnt,
        hiddenCnt,
        twinCnt,
        trackerCnt,
        hackCnt
    );
    tft.setTextColor((twinCnt > 0 || wepCnt > 0 || hackCnt > 0 || trackerCnt > 0) ? TFT_ORANGE : TFT_CYAN, bg);
    tft.drawString(String(threat).substring(0, max(10, w / LW)), x + 2, curY, 1);
    curY += LH + 4;

    // Channel bar chart (1..13) with military tactical brackets & golden peak caps
    int chartTop = curY;
    int chartBot = y + h - LH - 4;
    int chartH = max(20, chartBot - chartTop);
    tft.drawFastHLine(x + 2, chartBot, w - 4, COL_QIANKUN_DIM);
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
            tft.drawFastHLine(bx, chartBot - bh, barW, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, bg);
            tft.drawCentreString(String(chCount[c]), bx + barW / 2, chartBot - bh - LH - 1, 1);
        }
        // Highlight non-overlapping standard channels 1, 6, 11
        tft.setTextColor((c == 1 || c == 6 || c == 11) ? COL_QIANKUN_GOLD : TFT_LIGHTGREY, bg);
        tft.drawCentreString(String(c), bx + barW / 2, chartBot + 2, 1);
    }
}

// =====================================================================
// DEVICE INSPECTOR DETAIL CARD
// =====================================================================
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

    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(
        bruce_i18n::tr("SEL=Fox-Hunt  UP=Triangulate  ESC=Back"),
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
        String prefix = (t.flags & (RFLAG_EVIL_TWIN | RFLAG_TRACKER | RFLAG_HACK_DEV))
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

    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(
        bruce_i18n::tr("Press any key to return"), tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1
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

void radar_setup(RadarInitialMode initialMode, RadarInitialView initialView) {
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

    auto drawLayout = [&]() {
        drawMainBorderWithTitle(viewName(view, ctx.hasLocked));
        tft.setTextSize(FP);
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        const char *hint = (view == RadarView::TABLE) ? "UP/DN=Select  LR=View  SEL=Act"
                                                      : "LR=View  UP/DN=Filter  SEL=Menu";
        tft.drawCentreString(bruce_i18n::tr(hint), tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1);
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
    uint32_t lastFoxSampleMs = 0;
    uint32_t lastLocSyncMs = 0;
    uint32_t lastContactBeepMs = 0;
    uint32_t lastSweepBeepMs = 0;
    size_t prevTargetCount = 0;
    size_t prevAlertCount = 0;
    bool exitRadar = false;

    auto radarEmitBeep = [&](unsigned int freq, unsigned long durMs) {
        if (!bruceConfig.radarBeep || !bruceConfig.soundEnabled) return;
#if defined(HAS_SPEAKER)
        if (isAudioPlaying()) return;
        playTone(freq, durMs, 2, PLAYBACK_ASYNC);
#else
        _tone(freq, durMs, PLAYBACK_ASYNC);
#endif
    };

    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);

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
                    // Record strongest nearby Wi-Fi/BLE devices with estimated GPS coords as persistent anchors
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

        // 5) Render active view + tactical sonar/contact beep feedback
        auto checkContactBeep = [&](const std::vector<RadarTarget> &list, bool sweepWrapped) {
            if (!bruceConfig.radarBeep || !bruceConfig.soundEnabled) return;
            size_t alerts = 0;
            bool hasClose = false;
            for (const auto &t : list) {
                if (t.flags & (RFLAG_TRACKER | RFLAG_EVIL_TWIN | RFLAG_HACK_DEV)) ++alerts;
                if (t.distM <= 3.0f) hasClose = true;
            }
            if (alerts > prevAlertCount && (now - lastContactBeepMs >= 900)) {
                lastContactBeepMs = now;
                radarEmitBeep(2150, 60);
            } else if (list.size() > prevTargetCount && (now - lastContactBeepMs >= 1100)) {
                lastContactBeepMs = now;
                radarEmitBeep(hasClose ? 1760 : 1520, 42);
            } else if (sweepWrapped && !list.empty() && !ctx.hasLocked && (now - lastSweepBeepMs >= 2000)) {
                lastSweepBeepMs = now;
                radarEmitBeep(hasClose ? 1320 : 1080, 48);
            }
            prevTargetCount = list.size();
            prevAlertCount = alerts;
        };

        if (view == RadarView::SCOPE) {
            if (now - lastFrameMs >= 60) {
                pruneStaleTargets(ctx);
                std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                sweepRad += 0.16f;
                bool wrapped = false;
                if (sweepRad >= 2.0f * RADAR_PI) {
                    sweepRad -= 2.0f * RADAR_PI;
                    wrapped = true;
                }
                drawRadarScope(scopeCx, scopeCy, scopeR, sweepRad, visible, ctx);
                checkContactBeep(visible, wrapped);
                lastFrameMs = now;

                if (wideLayout && (now - lastHudMs >= 320 || lastHudMs == 0)) {
                    drawRadarHud(
                        hudX, contentTop, hudW, contentH, mode, timeSliced, wifiScanning, bleScanning, visible, ctx
                    );
                    lastHudMs = now;
                }
            }
        } else if (now - lastHudMs >= 350 || lastHudMs == 0) {
            pruneStaleTargets(ctx);
            if (view == RadarView::TABLE) {
                std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
                drawIntelTable(
                    contentL, contentTop, contentW, contentH, mode, timeSliced, wifiScanning, bleScanning,
                    visible, ctx
                );
                checkContactBeep(visible, false);
            } else {
                std::vector<RadarTarget> allList = snapshotTargets(ctx, mode, true);
                drawFoxHuntOrSpectrum(contentL, contentTop, contentW, contentH, allList, ctx);
            }
            lastHudMs = now;
        }

        // 6) Controls
        auto cycleView = [&](int dir) {
            int next = ((int)view + dir + 3) % 3;
            view = static_cast<RadarView>(next);
            drawLayout();
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
            ctx.tableCursor = 0;
            ctx.tableScroll = 0;
            lastSweepEndMs = 0;
            lastHudMs = 0;
        };

        if (check(EscPress)) {
            exitRadar = true;
        } else if (check(PrevPress)) {
            cycleView(-1);
        } else if (check(NextPress)) {
            cycleView(1);
        } else if (check(UpPress)) {
            if (view == RadarView::TABLE) {
                if (ctx.tableCursor > 0) --ctx.tableCursor;
                lastHudMs = 0;
            } else {
                cycleFilter(-1);
            }
        } else if (check(DownPress)) {
            if (view == RadarView::TABLE) {
                ++ctx.tableCursor;
                lastHudMs = 0;
            } else {
                cycleFilter(1);
            }
        } else if (check(SelPress)) {
            stopAllScans();
#if defined(HAS_SPEAKER)
    if (isAudioPlaying()) stopAudioPlayback();
#endif
            std::vector<RadarTarget> visible = snapshotTargets(ctx, mode);
            bool hasTableSelection =
                (view == RadarView::TABLE && !visible.empty() && ctx.tableCursor >= 0 &&
                 ctx.tableCursor < (int)visible.size());
            RadarTarget focused = hasTableSelection ? visible[ctx.tableCursor] : RadarTarget{};

            enum RadarMenuAction : uint8_t {
                ACT_NONE = 0,
                ACT_INSPECT,
                ACT_FOXHUNT,
                ACT_TRIANGULATE,
                ACT_UNLOCK,
                ACT_GEIGER,
                ACT_RADAR_BEEP,
                ACT_CYCLE_VIEW,
                ACT_CYCLE_FILTER,
                ACT_LABELS,
                ACT_LOC_INFO,
                ACT_SET_GPS_ANCHOR,
                ACT_SAVE_LOC,
                ACT_EXPORT_CSV,
                ACT_RESCAN,
                ACT_EXIT,
            };
            RadarMenuAction act = ACT_NONE;

            // Flat local menu vector: callbacks ONLY set `act`, so nested dialogs run after loopOptions returns!
            std::vector<Option> menuOpts;
            menuOpts.push_back({"Inspect Target", [&]() { act = ACT_INSPECT; }});
            menuOpts.push_back({"Fox-Hunt / Geiger", [&]() { act = ACT_FOXHUNT; }});
            menuOpts.push_back({"Locate / Triangulate", [&]() { act = ACT_TRIANGULATE; }});
            if (ctx.hasLocked) {
                menuOpts.push_back({"Unlock Target", [&]() { act = ACT_UNLOCK; }});
                menuOpts.push_back({ctx.geigerAudio ? "Geiger Beep: ON" : "Geiger Beep: OFF",
                                    [&]() { act = ACT_GEIGER; }});
            }
            menuOpts.push_back({bruceConfig.radarBeep ? "Radar Beep: ON" : "Radar Beep: OFF",
                                [&]() { act = ACT_RADAR_BEEP; }});
            menuOpts.push_back({String("View: ") + viewName(view, ctx.hasLocked),
                                [&]() { act = ACT_CYCLE_VIEW; }});
            menuOpts.push_back({modeLabel(mode), [&]() { act = ACT_CYCLE_FILTER; }});
            menuOpts.push_back({ctx.showLabels ? "Labels: ON" : "Labels: OFF", [&]() { act = ACT_LABELS; }});
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
                    bool have = hasTableSelection || pickTarget(ctx, visible, "Inspect Target", chosen);
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
                    bool have = hasTableSelection || pickTarget(ctx, visible, "Fox-Hunt / Geiger", chosen);
                    if (have) {
                        lockOnTarget(ctx, chosen);
                        view = RadarView::FOX_SPEC;
                    }
                    break;
                }
                case ACT_TRIANGULATE: {
                    RadarTarget chosen = focused;
                    bool have = hasTableSelection;
                    if (!have && ctx.hasLocked) {
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
                        lockOnTarget(ctx, chosen);
                        runTriangulateWizard(ctx, chosen, timeSliced, wifiAvailable, bleAvailable, radarScan);
                        if (bruceLocation.hasLocation()) bruceLocation.save();
                    }
                    break;
                }
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
                    view = static_cast<RadarView>(((int)view + 1) % 3);
                    break;
                case ACT_CYCLE_FILTER:
                    mode = static_cast<RadarMode>(((int)mode + 1) % 5);
                    nextIsBle = (mode == RadarMode::BLE_ONLY);
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
                    lastSweepEndMs = 0;
                    break;
                case ACT_EXIT:
                    exitRadar = true;
                    break;
                default:
                    break;
            }

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
