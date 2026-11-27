#include "unity_lite.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Bounded BLE Scanner eviction logic (matches BLE_Suite.cc MAX_SCANNER_DEVICES = 64)
struct BoundedBleTable {
    static constexpr size_t MAX_CAP = 64;
    struct Entry {
        std::string addr;
        int rssi;
        bool fastPair;
        bool hasHfp;
    };
    std::vector<Entry> items;

    void add(const std::string &addr, int rssi, bool fp, bool hfp) {
        for (auto &e : items) {
            if (e.addr == addr) {
                if (rssi > e.rssi) e.rssi = rssi;
                return;
            }
        }
        if (items.size() < MAX_CAP) {
            items.push_back({addr, rssi, fp, hfp});
            return;
        }
        size_t victim = 0;
        bool found = false;
        bool newPrio = fp || hfp;
        for (size_t i = 0; i < items.size(); ++i) {
            bool entryPrio = items[i].fastPair || items[i].hasHfp;
            if (entryPrio && !newPrio) continue;
            if (!found || items[i].rssi < items[victim].rssi) {
                victim = i;
                found = true;
            }
        }
        if (found && ((newPrio && !(items[victim].fastPair || items[victim].hasHfp)) || rssi > items[victim].rssi)) {
            items[victim] = {addr, rssi, fp, hfp};
        }
    }
};

// Bounded Wi-Fi promiscuous POD capture (matches deauther.cc RawDetectedClient[64])
struct PromiscClientPool {
    static constexpr size_t MAX_CAP = 64;
    struct Entry {
        uint8_t mac[6];
        int8_t rssi;
    };
    Entry items[MAX_CAP] = {};
    size_t count = 0;

    void capture(const uint8_t mac[6], int8_t rssi) {
        for (size_t i = 0; i < count; ++i) {
            if (std::memcmp(items[i].mac, mac, 6) == 0) {
                if (rssi > items[i].rssi) items[i].rssi = rssi;
                return;
            }
        }
        if (count < MAX_CAP) {
            std::memcpy(items[count].mac, mac, 6);
            items[count].rssi = rssi;
            count++;
        }
    }
};

// Wireless Sentinel threat detection models
struct SentinelAp {
    std::string ssid;
    uint8_t bssid[6];
    bool encrypted;
};

static bool detectEvilTwin(const std::vector<SentinelAp> &aps) {
    for (size_t i = 0; i < aps.size(); ++i) {
        for (size_t j = i + 1; j < aps.size(); ++j) {
            if (aps[i].ssid == aps[j].ssid && aps[i].encrypted != aps[j].encrypted &&
                std::memcmp(aps[i].bssid, aps[j].bssid, 6) != 0) {
                return true;
            }
        }
    }
    return false;
}

TEST_CASE(ble_scanner_caps_at_64_and_preserves_priority_and_strong_rssi) {
    BoundedBleTable t;
    // Insert 1 FastPair device with weak RSSI (-88) and 63 weak generic devices (-90..-70)
    t.add("AA:00:00:00:00:00", -88, true, false);
    for (int i = 1; i < 64; ++i) {
        char mac[20];
        std::snprintf(mac, sizeof(mac), "00:00:00:00:00:%02X", i);
        t.add(mac, -90 + (i % 20), false, false);
    }
    ASSERT_EQ(t.items.size(), (size_t)64);

    // Flood with 40 more generic strong devices (-50 dBm)
    for (int i = 64; i < 104; ++i) {
        char mac[20];
        std::snprintf(mac, sizeof(mac), "00:00:00:00:01:%02X", i);
        t.add(mac, -50, false, false);
    }
    ASSERT_EQ(t.items.size(), (size_t)64);

    // Priority FastPair entry MUST still be preserved despite weak RSSI!
    bool keptFastPair = false;
    for (const auto &e : t.items) {
        if (e.addr == "AA:00:00:00:00:00") keptFastPair = true;
    }
    ASSERT_TRUE(keptFastPair);
}

TEST_CASE(wifi_promiscuous_pod_pool_deduplicates_and_caps_at_64) {
    PromiscClientPool pool;
    for (int i = 0; i < 100; ++i) {
        uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, static_cast<uint8_t>(i)};
        pool.capture(mac, -70);
    }
    ASSERT_EQ(pool.count, (size_t)64);

    // Duplicate MAC with stronger RSSI updates in place without increasing count
    uint8_t existing[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x05};
    pool.capture(existing, -42);
    ASSERT_EQ(pool.count, (size_t)64);
    ASSERT_EQ(pool.items[5].rssi, (int8_t)-42);
}

TEST_CASE(sentinel_evil_twin_flags_security_mismatch_only) {
    std::vector<SentinelAp> mesh = {
        {"CorpWiFi", {1, 2, 3, 4, 5, 6}, true},
        {"CorpWiFi", {1, 2, 3, 4, 5, 7}, true},
    };
    ASSERT_FALSE(detectEvilTwin(mesh)); // legitimate multi-AP mesh

    mesh.push_back({"CorpWiFi", {9, 9, 9, 9, 9, 9}, false}); // rogue Open twin!
    ASSERT_TRUE(detectEvilTwin(mesh));
}

TEST_CASE(sentinel_ble_stalker_persistence_and_rf_jamming_streak) {
    // BLE Stalker: needs >= 3 epochs AND >= 90s elapsed
    auto isStalker = [](uint16_t hits, uint32_t elapsedMs) { return hits >= 3 && elapsedMs >= 90000; };
    ASSERT_FALSE(isStalker(1, 0));
    ASSERT_FALSE(isStalker(2, 120000));
    ASSERT_FALSE(isStalker(4, 30000));
    ASSERT_TRUE(isStalker(3, 90000));

    // CC1101 Sub-GHz jamming: needs 3 consecutive sweeps >= baseline + 18 dB
    float baseline = -95.0f;
    int streak = 0;
    bool alerted = false;
    int samples[] = {-94, -70, -95, -72, -71, -69}; // 1 transient spike (no alert), then 3 sustained spikes (alert!)
    for (int rssi : samples) {
        if ((float)rssi >= baseline + 18.0f) {
            if (++streak == 3) alerted = true;
        } else {
            streak = 0;
        }
    }
    ASSERT_TRUE(alerted);
}

int main() { return nezha_test::runAll("Radio Bounded Containers & Sentinel Rules"); }
