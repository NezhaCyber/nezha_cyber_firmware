#include "Audio.h"
#include "config.h"
#include "input_manager.h"
#include "pins.h"
#include "radio_ui.h"
#include "storage_manager.h"
#include <Arduino.h>
#include <WiFi.h>

namespace {

Audio g_audio;
RadioUI g_ui;
InputManager g_input;
StorageManager g_storage;

std::vector<RadioStation> g_stations;
RadioUiState g_state;
WifiCred g_wifiCred;

bool g_uiDirty = true;
uint32_t g_lastTelemetryMs = 0;
uint32_t g_lastWifiRetryMs = 0;

void updateCodecFromInfo(const String &info) {
    if (info.indexOf("MP3") >= 0 || info.indexOf("mp3") >= 0) g_state.codec = "MP3";
    else if (info.indexOf("AAC") >= 0 || info.indexOf("aac") >= 0) g_state.codec = "AAC";
    else if (info.indexOf("FLAC") >= 0) g_state.codec = "FLAC";
    else if (info.indexOf("OGG") >= 0 || info.indexOf("Vorbis") >= 0) g_state.codec = "OGG";
}

void tuneStation(int index) {
    if (g_stations.empty()) return;
    int count = static_cast<int>(g_stations.size());
    index = ((index % count) + count) % count;

    g_state.stationIndex = index;
    g_state.stationCount = count;
    g_state.stationName = g_stations[index].name;
    g_state.stationGenre = g_stations[index].genre;
    g_state.icyTitle = "Buffering stream...";
    g_state.codec = "---";
    g_state.bitrateKbps = 0;
    g_state.statusText = "TUNING";
    g_ui.render(g_state, false);

    g_storage.saveRadioPrefs(index, g_state.volume);

    Serial.printf("[RADIO] Tuning [%d/%d] %s -> %s\n",
                  index + 1, count, g_stations[index].name.c_str(), g_stations[index].url.c_str());
    g_audio.stopSong();
    delay(40);
    g_audio.connecttohost(g_stations[index].url.c_str());
    g_uiDirty = true;
}

void applyVolume() {
    g_audio.setVolume(g_state.muted ? 0 : g_state.volume);
    g_storage.saveRadioPrefs(g_state.stationIndex, g_state.volume);
    g_uiDirty = true;
}

bool connectWifi(uint32_t timeoutMs = 15000) {
    if (g_wifiCred.ssid.isEmpty()) return false;
    g_ui.showStatusOverlay("Connecting Wi-Fi", g_wifiCred.ssid, "Please wait...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Prevent audio dropouts from Wi-Fi modem sleep
    WiFi.begin(g_wifiCred.ssid.c_str(), g_wifiCred.password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(200);
    }

    g_state.wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (g_state.wifiConnected) {
        g_state.wifiRssi = WiFi.RSSI();
        g_state.ipAddress = WiFi.localIP().toString();
        Serial.printf("[WIFI] Connected: %s (%d dBm)\n", g_state.ipAddress.c_str(), g_state.wifiRssi);
        return true;
    }
    Serial.println("[WIFI] Connection failed");
    return false;
}

void runInteractiveWifiSetup() {
    g_audio.stopSong();
    static const char *s_prompt = "Wi-Fi SSID";
    auto redrawSsid = [](const String &val) {
        g_ui.showStatusOverlay("Wi-Fi Setup (1/2)", String(s_prompt) + ":", val + "_");
    };
    String ssid = g_input.readLineBlocking("Enter Wi-Fi SSID", false, redrawSsid);
    ssid.trim();
    if (ssid.isEmpty()) return;

    auto redrawPass = [](const String &val) {
        g_ui.showStatusOverlay("Wi-Fi Setup (2/2)", "Password:", val + "_");
    };
    String pass = g_input.readLineBlocking("Enter Wi-Fi Password", true, redrawPass);

    g_wifiCred.ssid = ssid;
    g_wifiCred.password = pass;
    g_storage.saveWifiCreds(g_wifiCred);

    if (connectWifi()) {
        g_ui.render(g_state, true);
        tuneStation(g_state.stationIndex);
    } else {
        g_ui.showStatusOverlay("Wi-Fi Failed", "Press 'W' to retry", "or check /wifi.json on SD");
    }
}

} // namespace

// ============================================================================
// ESP32-audioI2S Weak Metadata & Telemetry Callbacks
// ============================================================================
void audio_info(const char *info) {
    if (!info) return;
    Serial.printf("[AUDIO] %s\n", info);
    String s(info);
    updateCodecFromInfo(s);
    if (s.indexOf("BitRate:") >= 0) {
        int idx = s.indexOf("BitRate:");
        long bps = s.substring(idx + 8).toInt();
        if (bps > 1000) g_state.bitrateKbps = static_cast<uint32_t>(bps / 1000);
    }
    if (s.indexOf("Stream ready") >= 0 || s.indexOf("Audio-Length") >= 0 || s.indexOf("syncword") >= 0) {
        g_state.statusText = "LIVE";
    }
    g_uiDirty = true;
}

void audio_showstation(const char *info) {
    if (info && info[0] != '\0') {
        Serial.printf("[STATION] %s\n", info);
        g_state.statusText = "LIVE";
        g_uiDirty = true;
    }
}

void audio_showstreamtitle(const char *info) {
    if (info && info[0] != '\0') {
        Serial.printf("[ICY] %s\n", info);
        g_state.icyTitle = String(info);
        g_state.statusText = "LIVE";
        g_uiDirty = true;
    }
}

void audio_bitrate(const char *info) {
    if (info && info[0] != '\0') {
        long bps = atol(info);
        if (bps > 1000) g_state.bitrateKbps = static_cast<uint32_t>(bps / 1000);
        else if (bps > 0) g_state.bitrateKbps = static_cast<uint32_t>(bps);
        g_uiDirty = true;
    }
}

void audio_eof_stream(const char *info) {
    Serial.printf("[AUDIO] Stream EOF: %s — reconnecting...\n", info ? info : "");
    g_state.statusText = "RECONN";
    g_uiDirty = true;
}

void setup() {
    // 1. STRICT HARDWARE REQUIREMENT: Enable T-Deck peripheral power rail FIRST!
    pinMode(PIN_BOARD_POWERON, OUTPUT);
    digitalWrite(PIN_BOARD_POWERON, HIGH);
    delay(60);

    // Deselect all shared SPI chip-select lines cleanly before initializing SPI bus
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);

    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== LILYGO T-Deck Standalone Wi-Fi Internet Radio ===");
    Serial.printf("[MEM] PSRAM: %s (%u KB free)\n",
                  psramFound() ? "OK" : "MISSING", (unsigned)(ESP.getFreePsram() / 1024));

    // 2. Display (ST7789 320x240 landscape)
    g_ui.begin();
    g_ui.showSplash("Initializing SD, I2S & Controls...");

    // 3. MicroSD + Presets + NVS
    g_state.sdMounted = g_storage.beginSd();
    g_stations = g_storage.loadStations();
    g_state.stationCount = static_cast<int>(g_stations.size());
    g_storage.loadRadioPrefs(g_state.stationIndex, g_state.volume);
    if (g_state.stationIndex < 0 || g_state.stationIndex >= g_state.stationCount) {
        g_state.stationIndex = 0;
    }

    // 4. Inputs (Trackball Hall ISRs + ESP32-C3 I2C Keyboard)
    g_input.begin();

    // 5. MAX98357A I2S Speaker Engine (PSRAM-backed input ring buffer)
    g_audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
    g_audio.setVolume(g_state.volume);
    g_audio.setConnectionTimeout(1500, 3500);

    // 6. Wi-Fi connect (from /wifi.json -> NVS -> interactive keyboard/Serial setup)
    if (!g_storage.loadWifiCreds(g_wifiCred) || !connectWifi()) {
        runInteractiveWifiSetup();
    }

    // 7. Render dashboard & begin streaming
    g_state.freePsramKb = ESP.getFreePsram() / 1024;
    g_ui.render(g_state, true);
    if (g_state.wifiConnected) {
        tuneStation(g_state.stationIndex);
    }
}

void loop() {
    // Keep I2S audio decoder fed tightly to avoid dropouts
    g_audio.loop();

    // Handle trackball & I2C keyboard input
    InputEvent ev = g_input.poll();
    switch (ev) {
        case InputEvent::VOL_UP:
            if (g_state.muted) g_state.muted = false;
            g_state.volume = min(RADIO_VOL_MAX, g_state.volume + 1);
            applyVolume();
            break;
        case InputEvent::VOL_DOWN:
            if (g_state.muted) g_state.muted = false;
            g_state.volume = max(RADIO_VOL_MIN, g_state.volume - 1);
            applyVolume();
            break;
        case InputEvent::STATION_PREV:
            tuneStation(g_state.stationIndex - 1);
            break;
        case InputEvent::STATION_NEXT:
            tuneStation(g_state.stationIndex + 1);
            break;
        case InputEvent::TOGGLE_MUTE:
            g_state.muted = !g_state.muted;
            applyVolume();
            break;
        case InputEvent::WIFI_SETUP:
            runInteractiveWifiSetup();
            break;
        case InputEvent::RECONNECT:
            tuneStation(g_state.stationIndex);
            break;
        case InputEvent::PRESET_1:
        case InputEvent::PRESET_2:
        case InputEvent::PRESET_3:
        case InputEvent::PRESET_4:
        case InputEvent::PRESET_5:
        case InputEvent::PRESET_6:
        case InputEvent::PRESET_7:
        case InputEvent::PRESET_8:
        case InputEvent::PRESET_9: {
            int p = static_cast<int>(ev) - static_cast<int>(InputEvent::PRESET_1);
            if (p < static_cast<int>(g_stations.size())) tuneStation(p);
            break;
        }
        default:
            break;
    }

    uint32_t now = millis();

    // Periodic telemetry refresh (~1 Hz)
    if (now - g_lastTelemetryMs >= 1000) {
        g_lastTelemetryMs = now;
        g_state.wifiConnected = (WiFi.status() == WL_CONNECTED);
        g_state.wifiRssi = g_state.wifiConnected ? WiFi.RSSI() : -100;
        if (g_state.wifiConnected) g_state.ipAddress = WiFi.localIP().toString();
        g_state.freePsramKb = ESP.getFreePsram() / 1024;

        uint32_t filled = g_audio.inBufferFilled();
        uint32_t freeB = g_audio.inBufferFree();
        uint32_t total = filled + freeB;
        g_state.bufferHealthPct = (total > 0) ? static_cast<uint8_t>((filled * 100u) / total) : 0;

        if (g_audio.isRunning() && g_state.statusText != "LIVE") {
            g_state.statusText = "LIVE";
        }
        g_uiDirty = true;

        // Auto-reconnect Wi-Fi if dropped
        if (!g_state.wifiConnected && !g_wifiCred.ssid.isEmpty() && (now - g_lastWifiRetryMs) > 10000) {
            g_lastWifiRetryMs = now;
            g_state.statusText = "NO WIFI";
            WiFi.reconnect();
        }
    }

    if (g_uiDirty) {
        g_ui.render(g_state, false);
        g_uiDirty = false;
    } else {
        g_ui.tickMarquee(g_state);
    }
}
