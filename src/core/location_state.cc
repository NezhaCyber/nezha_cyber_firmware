#include "location_state.h"

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline bool isChinese() { return false; }
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
} // namespace bruce_i18n
#endif
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include <ArduinoJson.h>
#include <cmath>
#include <globals.h>

BruceLocationState bruceLocation;

namespace {

constexpr double METERS_PER_DEG_LAT = 111320.0;
constexpr double LOC_DEG_TO_RAD = 3.14159265358979323846 / 180.0;

static bool parseMacStr(const char *s, uint8_t out[6]) {
    if (!s) return false;
    unsigned int b[6];
    if (sscanf(s, "%02X:%02X:%02X:%02X:%02X:%02X", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(b[i]);
    return true;
}

static String macToStr(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static FS *pickLocationFs(bool preferExisting) {
    if (preferExisting) {
        if (sdcardMounted && SD.exists(BruceLocationState::STATE_PATH)) return &SD;
        if (checkLittleFsSize() && LittleFS.exists(BruceLocationState::STATE_PATH)) return &LittleFS;
    }
    if (sdcardMounted) return &SD;
    if (checkLittleFsSize()) return &LittleFS;
    return nullptr;
}

} // namespace

void BruceLocationState::ensureLoaded() {
    if (_loaded) return;
    _loaded = true;
    load();
}

bool BruceLocationState::load() {
    FS *fs = pickLocationFs(true);
    if (!fs || !fs->exists(STATE_PATH)) return false;

    File f = fs->open(STATE_PATH, FILE_READ);
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    _valid = doc["valid"] | false;
    _lat = doc["lat"] | 0.0;
    _lon = doc["lon"] | 0.0;
    _alt = doc["alt"] | 0.0;
    _sats = doc["sats"] | 0u;
    String savedSrc = doc["source"] | "SAVED";
    _source = _valid ? (savedSrc == "GPS" ? "SAVED" : savedSrc) : "NONE";

    _anchors.clear();
    JsonArray arr = doc["anchors"].as<JsonArray>();
    for (JsonObject obj : arr) {
        if (_anchors.size() >= MAX_ANCHORS) break;
        RfLocationAnchor a;
        if (!parseMacStr(obj["mac"] | "", a.mac)) continue;
        a.isWifi = (String(obj["radio"] | "W") == "W");
        const char *nm = obj["name"] | "";
        strncpy(a.name, nm, sizeof(a.name) - 1);
        a.name[sizeof(a.name) - 1] = '\0';
        a.lat = obj["lat"] | 0.0;
        a.lon = obj["lon"] | 0.0;
        a.rssi = static_cast<int8_t>(obj["rssi"] | -80);
        if (fabs(a.lat) > 0.0001 || fabs(a.lon) > 0.0001) {
            _anchors.push_back(a);
        }
    }
    return _valid || !_anchors.empty();
}

bool BruceLocationState::save() {
    FS *fs = pickLocationFs(false);
    if (!fs) return false;

    JsonDocument doc;
    doc["valid"] = _valid;
    doc["lat"] = _lat;
    doc["lon"] = _lon;
    doc["alt"] = _alt;
    doc["sats"] = _sats;
    doc["source"] = _source;
    doc["ms"] = millis();

    JsonArray arr = doc["anchors"].to<JsonArray>();
    for (const auto &a : _anchors) {
        JsonObject obj = arr.add<JsonObject>();
        obj["mac"] = macToStr(a.mac);
        obj["radio"] = a.isWifi ? "W" : "B";
        if (a.name[0]) obj["name"] = a.name;
        obj["lat"] = a.lat;
        obj["lon"] = a.lon;
        obj["rssi"] = (int)a.rssi;
    }

    File f = fs->open(STATE_PATH, FILE_WRITE);
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    _lastSaveMs = millis();
    return true;
}

void BruceLocationState::updateGps(double lat, double lon, double alt, uint32_t sats) {
    if (fabs(lat) < 0.0001 && fabs(lon) < 0.0001) return;
    _valid = true;
    _lat = lat;
    _lon = lon;
    _alt = alt;
    _sats = sats;
    _source = "GPS";
    if (_lastSaveMs == 0 || (millis() - _lastSaveMs) >= 15000) {
        save();
    }
}

void BruceLocationState::setManualAnchor(double lat, double lon) {
    _valid = true;
    _lat = lat;
    _lon = lon;
    _alt = 0.0;
    _sats = 0;
    _source = "MANUAL";
    save();
}

void BruceLocationState::recordAnchor(
    const uint8_t mac[6], bool isWifi, const char *name, double targetLat, double targetLon, int8_t rssi
) {
    if (!_valid) return;
    if (fabs(targetLat) < 0.0001 && fabs(targetLon) < 0.0001) return;

    for (auto &a : _anchors) {
        if (a.isWifi == isWifi && memcmp(a.mac, mac, 6) == 0) {
            // Smooth coordinates if updated
            a.lat = (a.lat * 0.6) + (targetLat * 0.4);
            a.lon = (a.lon * 0.6) + (targetLon * 0.4);
            a.rssi = rssi;
            if (name && name[0]) {
                strncpy(a.name, name, sizeof(a.name) - 1);
                a.name[sizeof(a.name) - 1] = '\0';
            }
            return;
        }
    }

    RfLocationAnchor entry;
    memcpy(entry.mac, mac, 6);
    entry.isWifi = isWifi;
    if (name && name[0]) {
        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
    }
    entry.lat = targetLat;
    entry.lon = targetLon;
    entry.rssi = rssi;

    if (_anchors.size() < MAX_ANCHORS) {
        _anchors.push_back(entry);
    } else {
        // Replace weakest anchor if this one is stronger
        size_t weakIdx = 0;
        for (size_t i = 1; i < _anchors.size(); ++i) {
            if (_anchors[i].rssi < _anchors[weakIdx].rssi) weakIdx = i;
        }
        if (rssi > _anchors[weakIdx].rssi) _anchors[weakIdx] = entry;
    }
}

bool BruceLocationState::tryRfPositionFix(const std::vector<RfObservation> &obs, int &matchedCount) {
    matchedCount = 0;
    if (_anchors.empty() || obs.empty()) return false;

    double wSum = 0.0, latSum = 0.0, lonSum = 0.0;
    for (const auto &o : obs) {
        for (const auto &a : _anchors) {
            if (a.isWifi == o.isWifi && memcmp(a.mac, o.mac, 6) == 0) {
                double d = (o.distM > 0.5f) ? (double)o.distM : 0.5;
                double w = 1.0 / (d * d);
                latSum += a.lat * w;
                lonSum += a.lon * w;
                wSum += w;
                ++matchedCount;
                break;
            }
        }
    }
    if (matchedCount == 0 || wSum <= 0.0) return false;

    _valid = true;
    _lat = latSum / wSum;
    _lon = lonSum / wSum;
    _source = "RF-FIX";
    return true;
}

bool BruceLocationState::estimateTargetGps(float relX, float relY, double &outLat, double &outLon) const {
    if (!_valid) return false;
    double cosLat = cos(_lat * LOC_DEG_TO_RAD);
    if (fabs(cosLat) < 0.01) cosLat = 0.01;
    outLat = _lat + (double)relY / METERS_PER_DEG_LAT;
    outLon = _lon + (double)relX / (METERS_PER_DEG_LAT * cosLat);
    return true;
}

void BruceLocationState::showLocationInfoScreen() {
    ensureLoaded();
    drawMainBorderWithTitle("Location State");

    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 14;
    int cardY = BORDER_PAD_Y + 23;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 18 : 22);
    uint16_t surf = uiSurfaceColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;

    uiDrawCard(cardX, cardY, cardW, cardH, true);

    int x = cardX + 10;
    int w = cardW - 20;
    int y = cardY + (compact ? 8 : 12);
    int rowH = (FP == 1 && bruce_i18n::isChinese()) ? (compact ? 15 : 18) : (compact ? 13 : 17);

    // Status header row with pill badge
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), surf);
    tft.drawString(bruce_i18n::tr("Source"), x, y + 3, 1);
    String srcStr = _valid ? _source : String(bruce_i18n::tr("No Fix"));
    uint16_t srcCol = _valid ? TFT_DARKGREEN : TFT_RED;
    int16_t badgeW = bruce_i18n::utf8PixelWidth(srcStr, FP) + 12;
    uiDrawBadge(x + w - badgeW, y, srcStr, srcCol, 15);
    y += max(rowH, 18);
    tft.drawFastHLine(x, y - 2, w, uiBorderDimColor());
    y += 3;

    if (_valid) {
        char buf[36];
        snprintf(buf, sizeof(buf), "%.6f", _lat);
        uiDrawKeyValueRow(x, y, w, "Lat", String(buf), pri, surf);
        y += rowH;
        snprintf(buf, sizeof(buf), "%.6f", _lon);
        uiDrawKeyValueRow(x, y, w, "Lon", String(buf), pri, surf);
        y += rowH;
    }
    uiDrawKeyValueRow(
        x, y, w, "RF Anchors", String((unsigned)_anchors.size()) + " / " + String((unsigned)MAX_ANCHORS), sec, surf
    );
    y += rowH;
    if (y + LH <= cardY + cardH - 4) {
        uiDrawKeyValueRow(x, y, w, "File", String(STATE_PATH), bright, surf);
    }

    printCenterFootnote("SEL=Set Anchor  ESC=Back");

    check(SelPress);
    check(EscPress);
    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (check(SelPress)) {
            if (promptManualGpsAnchor()) {
                displaySuccess("GPS Anchor Saved", true);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool BruceLocationState::promptManualGpsAnchor() {
    String cur = "";
    if (_valid) {
        char buf[36];
        snprintf(buf, sizeof(buf), "%.5f,%.5f", _lat, _lon);
        cur = String(buf);
    }
    String input = keyboard(cur, 32, "Lat,Lon (e.g. 3.1390,101.6869)");
    if (input.isEmpty() || input == "\x1B") return false;
    int comma = input.indexOf(',');
    if (comma <= 0) {
        displayError("Format: lat,lon", true);
        return false;
    }
    double la = input.substring(0, comma).toDouble();
    double lo = input.substring(comma + 1).toDouble();
    if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0 || (fabs(la) < 0.0001 && fabs(lo) < 0.0001)) {
        displayError("Invalid coordinates", true);
        return false;
    }
    setManualAnchor(la, lo);
    return true;
}
