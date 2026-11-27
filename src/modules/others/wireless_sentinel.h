#ifndef __WIRELESS_SENTINEL_H__
#define __WIRELESS_SENTINEL_H__

#include <cstdint>

enum class SentinelMode : uint8_t {
    PATROL_ALL = 0,  // Alternates Wi-Fi <-> BLE <-> CC1101 (4 MB safe)
    WIFI_SHIELD = 1, // Continuous Wi-Fi Deauth / Evil-Twin / Probe-Storm watch
    BLE_SHIELD = 2,  // Continuous BLE Stalker-Tracker / Skimmer / Spam watch
    RF_SHIELD = 3,   // Continuous Sub-GHz CC1101 noise-floor / jamming watch
};

void wireless_sentinel_menu();
void wireless_sentinel_run(SentinelMode mode = SentinelMode::PATROL_ALL);

#endif // __WIRELESS_SENTINEL_H__
