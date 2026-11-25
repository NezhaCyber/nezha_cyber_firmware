#pragma once

#include <Arduino.h>
#include <vector>

struct RadioStation {
    String name;
    String url;
    String genre;
};

struct WifiCred {
    String ssid;
    String password;
};

constexpr int RADIO_VOL_MIN = 0;
constexpr int RADIO_VOL_MAX = 21;
constexpr int RADIO_VOL_DEFAULT = 12;

// Fallback station presets when /stations.json is absent on microSD
inline std::vector<RadioStation> getDefaultStations() {
    return {
        {"BBC World Service",   "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service",      "News / UK"},
        {"BFM 89.9 Business",   "https://playerservices.streamtheworld.com/api/livestream-redirect/BFM899AAC.aac", "News / MY"},
        {"TraxxFM 90.3",        "https://playerservices.streamtheworld.com/api/livestream-redirect/TRAXX_FM.mp3",  "Music / MY"},
        {"NPR Program Stream",  "https://npr-ice.streamguys1.com/live.mp3",                    "News / US"},
        {"SomaFM Groove Salad", "http://ice1.somafm.com/groovesalad-128-mp3",                  "Ambient"},
        {"Radio Paradise Main", "http://stream.radioparadise.com/aac-128",                     "Eclectic"},
    };
}
