#include "storage_manager.h"
#include "pins.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

bool StorageManager::beginSd() {
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    // Share HSPI bus with ST7789 display (SCLK=40, MISO=38, MOSI=41)
    SPI.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_SD_CS);
    _sdMounted = SD.begin(PIN_SD_CS, SPI, 16000000);
    Serial.printf("[SD] Mount %s\n", _sdMounted ? "OK" : "not present (using defaults)");
    return _sdMounted;
}

std::vector<RadioStation> StorageManager::loadStations() {
    if (_sdMounted && SD.exists("/stations.json")) {
        File f = SD.open("/stations.json", FILE_READ);
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                std::vector<RadioStation> list;
                JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["stations"].as<JsonArray>();
                for (JsonObject item : arr) {
                    const char *name = item["name"] | "";
                    const char *url = item["url"] | "";
                    const char *genre = item["genre"] | "Stream";
                    if (name[0] != '\0' && url[0] != '\0') {
                        list.push_back({String(name), String(url), String(genre)});
                    }
                }
                if (!list.empty()) {
                    Serial.printf("[SD] Loaded %u stations from /stations.json\n", (unsigned)list.size());
                    return list;
                }
            } else {
                Serial.printf("[SD] /stations.json parse error: %s\n", err.c_str());
            }
        }
    }
    Serial.println("[SD] Using built-in default station presets");
    return getDefaultStations();
}

bool StorageManager::loadWifiCreds(WifiCred &outCred) {
    // 1. Try SD card /wifi.json first
    if (_sdMounted && SD.exists("/wifi.json")) {
        File f = SD.open("/wifi.json", FILE_READ);
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                String ssid = doc["ssid"] | "";
                String pass = doc["password"] | "";
                if (!ssid.isEmpty()) {
                    outCred.ssid = ssid;
                    outCred.password = pass;
                    Serial.printf("[WIFI] Loaded SSID '%s' from /wifi.json\n", ssid.c_str());
                    return true;
                }
            }
        }
    }

    // 2. Fallback to NVS Preferences
    Preferences prefs;
    if (prefs.begin("tdeck_radio", true)) {
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        prefs.end();
        if (!ssid.isEmpty()) {
            outCred.ssid = ssid;
            outCred.password = pass;
            Serial.printf("[WIFI] Loaded SSID '%s' from NVS\n", ssid.c_str());
            return true;
        }
    }
    return false;
}

void StorageManager::saveWifiCreds(const WifiCred &cred) {
    Preferences prefs;
    if (prefs.begin("tdeck_radio", false)) {
        prefs.putString("ssid", cred.ssid);
        prefs.putString("pass", cred.password);
        prefs.end();
    }
    if (_sdMounted) {
        File f = SD.open("/wifi.json", FILE_WRITE);
        if (f) {
            JsonDocument doc;
            doc["ssid"] = cred.ssid;
            doc["password"] = cred.password;
            serializeJsonPretty(doc, f);
            f.close();
        }
    }
}

void StorageManager::loadRadioPrefs(int &stationIdx, int &volume) {
    Preferences prefs;
    if (prefs.begin("tdeck_radio", true)) {
        stationIdx = prefs.getInt("station", 0);
        volume = constrain(prefs.getInt("vol", RADIO_VOL_DEFAULT), RADIO_VOL_MIN, RADIO_VOL_MAX);
        prefs.end();
    }
}

void StorageManager::saveRadioPrefs(int stationIdx, int volume) {
    Preferences prefs;
    if (prefs.begin("tdeck_radio", false)) {
        prefs.putInt("station", stationIdx);
        prefs.putInt("vol", constrain(volume, RADIO_VOL_MIN, RADIO_VOL_MAX));
        prefs.end();
    }
}
