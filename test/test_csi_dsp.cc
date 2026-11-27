#include "modules/wifi/csi_radar_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace csi_radar;

static int failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::fprintf(stderr, "  FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static void fillSteadyIq(int8_t *buf, int pairs, float scale, float phaseOffset) {
    for (int i = 0; i < pairs; ++i) {
        float angle = static_cast<float>(i) * 0.25f + phaseOffset;
        float amp = (18.0f + 6.0f * std::sin(static_cast<float>(i) * 0.3f)) * scale;
        buf[2 * i] = static_cast<int8_t>(amp * std::sin(angle));
        buf[2 * i + 1] = static_cast<int8_t>(amp * std::cos(angle));
    }
}

int main() {
    std::printf("=== Wi-Fi CSI Radar DSP Unit Tests ===\n");

    // 1. Null / short / zero input safety
    {
        float mag[CSI_MAX_BINS];
        CHECK(computeNormalizedProfile(nullptr, 64, mag) == 0);
        int8_t zeros[64] = {};
        CHECK(computeNormalizedProfile(zeros, 64, mag) == 0);
        CHECK(computeNormalizedProfile(zeros, 2, mag) == 0);
        std::printf("  [PASS] null_and_zero_input_safety\n");
    }

    // 2. AGC gain invariance: doubling raw IQ scale yields identical normalized profile
    {
        int8_t iq1[112] = {}, iq2[112] = {};
        fillSteadyIq(iq1, 56, 1.0f, 0.0f);
        fillSteadyIq(iq2, 56, 2.0f, 0.0f);

        float m1[CSI_MAX_BINS], m2[CSI_MAX_BINS];
        int n1 = computeNormalizedProfile(iq1, 112, m1);
        int n2 = computeNormalizedProfile(iq2, 112, m2);
        CHECK(n1 == n2 && n1 == 52);

        float maxDiff = 0.0f;
        for (int i = 0; i < n1; ++i) {
            float d = std::fabs(m1[i] - m2[i]);
            if (d > maxDiff) maxDiff = d;
        }
        CHECK(maxDiff < 0.08f); // small int8 quantization tolerance only
        std::printf("  [PASS] agc_gain_invariance (maxDiff=%.4f)\n", maxDiff);
    }

    // 3. Calibration -> STILL -> Through-Wall MOTION -> Decay back to STILL
    {
        CsiMotionTracker tr;
        int8_t iq[112] = {};
        fillSteadyIq(iq, 56, 1.0f, 0.0f);

        // Calibrate on stationary multipath room
        for (int k = 0; k < 28; ++k) tr.feedCsi(iq, 112);
        CHECK(tr.state == MotionState::STILL);
        CHECK(tr.motionScore < 20);

        // Inject strong multipath perturbation (person walking behind wall)
        for (int k = 0; k < 12; ++k) {
            for (int i = 0; i < 56; ++i) {
                float wobble = (i % 2 == 0) ? (1.0f + 0.7f * std::sin(k * 0.9f + i)) : (1.0f - 0.5f * std::cos(k * 0.7f));
                float amp = 20.0f * wobble;
                iq[2 * i] = static_cast<int8_t>(amp * std::sin(i * 0.4f + k));
                iq[2 * i + 1] = static_cast<int8_t>(amp * std::cos(i * 0.4f - k));
            }
            tr.feedCsi(iq, 112);
        }
        CHECK(tr.state == MotionState::MOTION);
        CHECK(tr.motionScore >= 60);

        // Room becomes quiet again -> baseline recovers back to STILL
        fillSteadyIq(iq, 56, 1.0f, 0.0f);
        for (int k = 0; k < 120; ++k) tr.feedCsi(iq, 112);
        CHECK(tr.state == MotionState::STILL);
        std::printf("  [PASS] calibration_motion_and_still_recovery\n");
    }

    // 4. RSSI fallback mode: steady RSSI -> STILL; multipath RSSI jitter -> MOTION
    {
        CsiMotionTracker tr;
        for (int k = 0; k < 30; ++k) tr.feedRssiSample(-58);
        CHECK(tr.state == MotionState::STILL);

        for (int k = 0; k < 15; ++k) tr.feedRssiSample((k % 2 == 0) ? -45 : -72);
        CHECK(tr.state == MotionState::MOTION);
        std::printf("  [PASS] rssi_fallback_variance_detection\n");
    }

    if (failures > 0) {
        std::fprintf(stderr, "FAILED: %d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("=== ALL CSI DSP TESTS PASSED ===\n");
    return 0;
}
