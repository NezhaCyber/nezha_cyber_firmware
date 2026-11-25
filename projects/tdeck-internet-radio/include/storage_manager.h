#pragma once

#include "config.h"
#include <vector>

class StorageManager {
public:
    bool beginSd();
    bool isSdMounted() const { return _sdMounted; }

    // Loads /stations.json from SD if present; otherwise returns built-in presets
    std::vector<RadioStation> loadStations();

    // Loads Wi-Fi credentials from SD /wifi.json first, then NVS fallback
    bool loadWifiCreds(WifiCred &outCred);

    // Persists Wi-Fi credentials to NVS (and SD /wifi.json if mounted)
    void saveWifiCreds(const WifiCred &cred);

    // Persist/restore last station index and volume in NVS
    void loadRadioPrefs(int &stationIdx, int &volume);
    void saveRadioPrefs(int stationIdx, int volume);

private:
    bool _sdMounted = false;
};
