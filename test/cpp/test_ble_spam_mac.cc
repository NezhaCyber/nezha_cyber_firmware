// Host-native tests for the BLE Spam MAC rotation fix in
// src/modules/ble/ble_spam.cc (bleSpamMacToNimbleRnd / bleSpamRestartAdvertiserForMac).
//
// Replicas of:
//  - bleSpamMacToNimbleRnd() + bleSpamFastRandomMac() from ble_spam.cc
//  - the validation inside ble_hs_id_set_rnd(), NimBLE-Arduino 2.5.1,
//    src/nimble/nimble/host/src/ble_hs_id.c (vendored under .pio/libdeps)
//
// History: the pre-fix layout put the 0xC0 random-static bits on the little-endian
// LSB (index 0) instead of the MSB (index 5). NimBLE validates rnd_addr[5] & 0xc0
// and rejects anything that is neither 0x00 nor 0xC0 with BLE_HS_EINVAL, so ~50%
// of all MAC rotations silently kept the previous MAC on air.

#include "unity_lite.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// --- Replica of bleSpamFastRandomMac() + xorshift64* PRNG (ble_spam.cc) ---
uint64_t gRngState = 0x9E3779B97F4A7C15ULL;

uint64_t nextRand64() {
    uint64_t x = gRngState;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    gRngState = x;
    return x * 0x2545F4914F6CDD1DULL;
}

void fastRandomMac(uint8_t *mac) {
    uint64_t r1 = nextRand64();
    uint64_t r2 = nextRand64();
    mac[0] = (uint8_t)(r1 & 0xFF);
    mac[1] = (uint8_t)((r1 >> 8) & 0xFF);
    mac[2] = (uint8_t)((r1 >> 16) & 0xFF);
    mac[3] = (uint8_t)((r1 >> 24) & 0xFF);
    mac[4] = (uint8_t)(r2 & 0xFF);
    mac[5] = (uint8_t)((r2 >> 8) & 0xFF);
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

// --- Replica of the FIXED bleSpamMacToNimbleRnd() (ble_spam.cc) ---
// index 0 = LSB of the on-air address, index 5 = MSB; random-static type bits
// (0xC0) forced on the MSB so NimBLE always classifies the address as valid.
void macToNimbleRndFixed(const uint8_t *mac, uint8_t *addr_le) {
    addr_le[0] = mac[5];
    addr_le[1] = mac[4];
    addr_le[2] = mac[3];
    addr_le[3] = mac[2];
    addr_le[4] = mac[1];
    addr_le[5] = mac[0] | 0xC0; // random static type bits
}

// --- Replica of the PRE-FIX layout (the bug being fixed) ---
void macToNimbleRndLegacy(const uint8_t *mac, uint8_t *addr_le) {
    addr_le[0] = mac[5] | 0xC0; // was wrongly treated as the MSB
    addr_le[1] = mac[4];
    addr_le[2] = mac[3];
    addr_le[3] = mac[2];
    addr_le[4] = mac[1];
    addr_le[5] = mac[0];
}

// --- Replica of ble_hs_id_set_rnd() validation (NimBLE 2.5.1 ble_hs_id.c) ---
int nimbleValidateRnd(const uint8_t *rnd_addr) {
    // addr_type_byte = rnd_addr[5] & 0xc0 — must be 0x00 (NRPA) or 0xC0 (static)
    uint8_t addr_type_byte = rnd_addr[5] & 0xc0;

    // "Make sure random part of rnd_addr is not all ones or zeros."
    int ones = 0;
    for (int i = 0; i < 5; i++) ones += __builtin_popcount(rnd_addr[i]);
    ones += __builtin_popcount(rnd_addr[5] & 0x3f);

    if ((addr_type_byte != 0x00 && addr_type_byte != 0xc0) || (ones == 0 || ones == 46)) {
        return -1; // BLE_HS_EINVAL
    }
    return 0;
}

// Little-endian buffer -> on-air display order (display byte 0 = MSB).
void leToDisplay(const uint8_t *le, uint8_t *display) {
    for (int i = 0; i < 6; i++) display[i] = le[5 - i];
}

constexpr int kIterations = 20000;

} // namespace

TEST_CASE(fixed_layout_always_accepted_by_nimble) {
    for (int i = 0; i < kIterations; i++) {
        uint8_t mac[6];
        fastRandomMac(mac);
        uint8_t le[6];
        macToNimbleRndFixed(mac, le);
        ASSERT_EQ(nimbleValidateRnd(le), 0);
    }
}

TEST_CASE(fixed_layout_on_air_address_matches_generated_mac) {
    for (int i = 0; i < kIterations; i++) {
        uint8_t mac[6];
        fastRandomMac(mac);
        uint8_t le[6];
        macToNimbleRndFixed(mac, le);

        uint8_t onAir[6];
        leToDisplay(le, onAir);

        // Display bytes 1..5 unchanged, byte 0 gets the random-static type bits.
        for (int b = 1; b < 6; b++) ASSERT_EQ(onAir[b], mac[b]);
        ASSERT_EQ(onAir[0], (uint8_t)(mac[0] | 0xC0));
    }
}

TEST_CASE(legacy_layout_was_rejected_half_the_time) {
    // Regression guard documenting the original bug: with the 0xC0 bits on the
    // little-endian LSB, NimBLE accepts only when the random top bits of mac[0]
    // happen to be 00 or 11 (~50%). Assert it is far from always-accepted.
    int accepted = 0;
    for (int i = 0; i < kIterations; i++) {
        uint8_t mac[6];
        fastRandomMac(mac);
        uint8_t le[6];
        macToNimbleRndLegacy(mac, le);
        if (nimbleValidateRnd(le) == 0) accepted++;
    }
    ASSERT_TRUE(accepted < kIterations * 80 / 100);
}

TEST_CASE(fast_random_mac_invariants) {
    for (int i = 0; i < kIterations; i++) {
        uint8_t mac[6];
        fastRandomMac(mac);
        ASSERT_EQ(mac[0] & 0x01, 0); // unicast bit
        ASSERT_EQ(mac[0] & 0x02, 2); // locally administered bit
    }
}

int main() { return nezha_test::runAll("ble_spam_mac"); }
