#ifndef __BRUCE_LOCATION_STATE_H__
#define __BRUCE_LOCATION_STATE_H__

#include <Arduino.h>
#include <cstdint>
#include <vector>

struct RfLocationAnchor {
    uint8_t mac[6] = {0};
    bool isWifi = false;
    char name[20] = {0};
    double lat = 0.0;
    double lon = 0.0;
    int8_t rssi = -90;
};

struct RfObservation {
    uint8_t mac[6];
    bool isWifi;
    int8_t rssi;
    float distM;
};

class BruceLocationState {
public:
    static constexpr size_t MAX_ANCHORS = 28;
    static constexpr const char *STATE_PATH = "/location_state.json";

    void ensureLoaded();
    bool load();
    bool save();

    bool hasLocation() const { return _valid; }
    bool hasLiveGps() const { return _valid && _source == "GPS"; }
    double lat() const { return _lat; }
    double lon() const { return _lon; }
    double alt() const { return _alt; }
    uint32_t sats() const { return _sats; }
    const String &source() const { return _source; }
    size_t anchorCount() const { return _anchors.size(); }
    const std::vector<RfLocationAnchor> &anchors() const { return _anchors; }

    // Update from live hardware GPS fix
    void updateGps(double lat, double lon, double alt, uint32_t sats);

    // Manually set a reference GPS anchor (enables real-world coords indoors without GPS)
    void setManualAnchor(double lat, double lon);

    // Record a nearby Wi-Fi AP or BLE device with its estimated GPS coordinate as an RF anchor
    void recordAnchor(
        const uint8_t mac[6], bool isWifi, const char *name, double targetLat, double targetLon, int8_t rssi
    );

    // Estimate scanner's GPS coordinates from currently visible Wi-Fi/BLE devices matching saved anchors
    bool tryRfPositionFix(const std::vector<RfObservation> &obs, int &matchedCount);

    // Convert relative (X=+East, Y=+North) meters from scanner into estimated GPS (lat, lon)
    bool estimateTargetGps(float relX, float relY, double &outLat, double &outLon) const;

    // Interactive UI helpers (usable from Radar, GPS, WiFi, BLE)
    void showLocationInfoScreen();
    bool promptManualGpsAnchor();

private:
    bool _loaded = false;
    bool _valid = false;
    double _lat = 0.0;
    double _lon = 0.0;
    double _alt = 0.0;
    uint32_t _sats = 0;
    uint32_t _lastSaveMs = 0;
    String _source = "NONE";
    std::vector<RfLocationAnchor> _anchors;
};

extern BruceLocationState bruceLocation;

#endif // __BRUCE_LOCATION_STATE_H__
