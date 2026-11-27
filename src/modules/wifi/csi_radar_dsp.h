#ifndef __CSI_RADAR_DSP_H__
#define __CSI_RADAR_DSP_H__

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace csi_radar {

static constexpr size_t CSI_MAX_BINS = 56;
static constexpr size_t CSI_WAVE_LEN = 96;

enum class MotionState : uint8_t {
    CALIBRATING = 0,
    STILL = 1,
    PRESENCE = 2,
    MOTION = 3,
};

enum class Sensitivity : uint8_t {
    Low = 0,
    Medium = 1,
    High = 2,
};

// Extract AGC-normalized subcarrier amplitude profile from raw ESP-IDF CSI I/Q bytes.
// ESP-IDF stores signed int8_t pairs (imag, real) per subcarrier.
// Normalizing by mean amplitude cancels automatic gain control (AGC) step artifacts.
inline int computeNormalizedProfile(const int8_t *iqBuf, int lenBytes, float outMag[CSI_MAX_BINS]) {
    if (!iqBuf || lenBytes < 4) return 0;
    int totalPairs = lenBytes / 2;
    if (totalPairs > (int)CSI_MAX_BINS) totalPairs = (int)CSI_MAX_BINS;

    float sum = 0.0f;
    int valid = 0;
    for (int i = 0; i < totalPairs; ++i) {
        // Skip first 2 and last 2 guard bins when full L-LTF/HT-LTF frame is present
        if (totalPairs >= 32 && (i < 2 || i >= totalPairs - 2)) continue;
        float im = static_cast<float>(iqBuf[2 * i]);
        float re = static_cast<float>(iqBuf[2 * i + 1]);
        float mag = std::sqrt(re * re + im * im);
        outMag[valid++] = mag;
        sum += mag;
    }

    if (valid == 0 || sum < 1e-3f) {
        for (int i = 0; i < valid; ++i) outMag[i] = 0.0f;
        return 0;
    }

    const float invMean = static_cast<float>(valid) / sum;
    for (int i = 0; i < valid; ++i) { outMag[i] *= invMean; }
    return valid;
}

// Pure fixed-memory POD motion & presence tracker (zero heap allocations).
struct CsiMotionTracker {
    float baseline[CSI_MAX_BINS] = {};
    float lastDelta[CSI_MAX_BINS] = {};
    int activeBins = 0;

    float noiseFloor = 0.03f;
    float smoothMetric = 0.0f;
    float rssiBaseline = -65.0f;
    bool rssiInit = false;

    uint8_t waveform[CSI_WAVE_LEN] = {};
    size_t waveHead = 0;

    uint32_t calibSamples = 0;
    uint32_t totalPackets = 0;
    uint8_t motionScore = 0; // 0..100
    MotionState state = MotionState::CALIBRATING;
    Sensitivity sensitivity = Sensitivity::Medium;

    static constexpr uint32_t CALIB_TARGET_SAMPLES = 24;

    void reset() {
        std::memset(baseline, 0, sizeof(baseline));
        std::memset(lastDelta, 0, sizeof(lastDelta));
        std::memset(waveform, 0, sizeof(waveform));
        activeBins = 0;
        noiseFloor = 0.03f;
        smoothMetric = 0.0f;
        rssiInit = false;
        waveHead = 0;
        calibSamples = 0;
        totalPackets = 0;
        motionScore = 0;
        state = MotionState::CALIBRATING;
    }

    float sensitivityGain() const {
        switch (sensitivity) {
            case Sensitivity::Low: return 0.65f;
            case Sensitivity::High: return 1.55f;
            case Sensitivity::Medium:
            default: return 1.0f;
        }
    }

    void pushWaveSample(uint8_t score) {
        waveform[waveHead] = score;
        waveHead = (waveHead + 1) % CSI_WAVE_LEN;
    }

    void updateStateFromMetric(float rawDelta) {
        totalPackets++;

        if (calibSamples < CALIB_TARGET_SAMPLES) {
            calibSamples++;
            if (calibSamples == 1) noiseFloor = rawDelta;
            else noiseFloor = noiseFloor * 0.85f + rawDelta * 0.15f;
            if (noiseFloor < 0.015f) noiseFloor = 0.015f;
            smoothMetric = smoothMetric * 0.7f + rawDelta * 0.3f;
            motionScore = 0;
            state = (calibSamples >= CALIB_TARGET_SAMPLES) ? MotionState::STILL : MotionState::CALIBRATING;
            pushWaveSample(0);
            return;
        }

        // Exponential smoothing suppresses single-packet multipath outliers
        smoothMetric = smoothMetric * 0.72f + rawDelta * 0.28f;

        const float floorRef = (noiseFloor > 0.015f) ? noiseFloor : 0.015f;
        const float ratio = (smoothMetric / floorRef) * sensitivityGain();

        // Map SNR-like ratio into 0..100 display score (1.0 = ambient floor -> ~8)
        float scaled = (ratio - 0.85f) * 28.0f;
        if (scaled < 0.0f) scaled = 0.0f;
        if (scaled > 100.0f) scaled = 100.0f;
        motionScore = static_cast<uint8_t>(scaled + 0.5f);

        if (ratio >= 3.2f) {
            state = MotionState::MOTION;
        } else if (ratio >= 1.85f) {
            state = MotionState::PRESENCE;
        } else {
            state = MotionState::STILL;
            // Slowly adapt ambient noise floor only when room is confirmed still
            noiseFloor = noiseFloor * 0.96f + rawDelta * 0.04f;
            if (noiseFloor < 0.015f) noiseFloor = 0.015f;
        }

        pushWaveSample(motionScore);
    }

    // Native ESP-IDF CSI feed (preferred high-sensitivity mode)
    bool feedCsi(const int8_t *iqBuf, int lenBytes) {
        float mag[CSI_MAX_BINS];
        int bins = computeNormalizedProfile(iqBuf, lenBytes, mag);
        if (bins < 8) return false;

        if (activeBins != bins) {
            for (int i = 0; i < bins; ++i) {
                baseline[i] = mag[i];
                lastDelta[i] = 0.0f;
            }
            activeBins = bins;
            updateStateFromMetric(0.02f);
            return true;
        }

        float deltaSum = 0.0f;
        // Adapt baseline faster during calibration, very slowly during active monitoring
        const float alpha = (calibSamples < CALIB_TARGET_SAMPLES) ? 0.18f : 0.025f;
        for (int i = 0; i < bins; ++i) {
            float d = std::fabs(mag[i] - baseline[i]);
            lastDelta[i] = d;
            deltaSum += d;
            baseline[i] = baseline[i] * (1.0f - alpha) + mag[i] * alpha;
        }

        float meanDelta = deltaSum / static_cast<float>(bins);
        updateStateFromMetric(meanDelta);
        return true;
    }

    // Graceful fallback when SDK lacks CONFIG_ESP_WIFI_CSI_ENABLED:
    // tracks RSSI multipath jitter from stationary AP beacons/data frames.
    void feedRssiSample(int8_t rssi) {
        float r = static_cast<float>(rssi);
        if (!rssiInit) {
            rssiBaseline = r;
            rssiInit = true;
            updateStateFromMetric(0.02f);
            return;
        }
        float diff = std::fabs(r - rssiBaseline);
        const float alpha = (calibSamples < CALIB_TARGET_SAMPLES) ? 0.20f : 0.04f;
        rssiBaseline = rssiBaseline * (1.0f - alpha) + r * alpha;
        // Normalize ~1 dB jitter -> ~0.04 metric scale matching CSI thresholds
        updateStateFromMetric(diff * 0.045f);
    }
};

} // namespace csi_radar

#endif // __CSI_RADAR_DSP_H__
