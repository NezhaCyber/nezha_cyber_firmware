#include "unity_lite.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct AnsiStripper {
    enum State : uint8_t { NORMAL = 0, ESC = 1, CSI = 2, OSC = 3 };
    State state = NORMAL;
    bool feed(uint8_t b) {
        switch (state) {
            case NORMAL:
                if (b == 0x1B) {
                    state = ESC;
                    return false;
                }
                return true;
            case ESC:
                if (b == '[') state = CSI;
                else if (b == ']') state = OSC;
                else state = NORMAL;
                return false;
            case CSI:
                if (b >= 0x40 && b <= 0x7E) state = NORMAL;
                return false;
            case OSC:
                if (b == 0x07 || b == 0x1B) state = NORMAL;
                return false;
        }
        state = NORMAL;
        return true;
    }
};

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) { crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1))); }
    }
    return ~crc;
}

static std::string decodeObdDtc(uint16_t raw) {
    static const char prefixChars[4] = {'P', 'C', 'B', 'U'};
    char code[8];
    std::snprintf(
        code,
        sizeof(code),
        "%c%u%03X",
        prefixChars[(raw >> 14) & 0x03],
        (unsigned)((raw >> 12) & 0x03),
        (unsigned)(raw & 0x0FFF)
    );
    return std::string(code);
}

static uint8_t sampleEdgeAt(uint8_t initLvl, const std::vector<uint32_t> &edges, uint64_t tick) {
    uint8_t lvl = initLvl;
    size_t lo = 0, hi = edges.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uint64_t)(edges[mid] >> 1) <= tick) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0) lvl = edges[lo - 1] & 1;
    return lvl;
}

TEST_CASE(ansi_stripper_removes_csi_and_osc_cleanly) {
    AnsiStripper st;
    const std::string raw = "\x1b[1;32mroot@openwrt\x1b[0m:\x1b]0;WindowTitle\x07/# ";
    std::string out;
    for (unsigned char c : raw) {
        if (st.feed(c)) out.push_back(static_cast<char>(c));
    }
    ASSERT_STREQ(out.c_str(), "root@openwrt:/# ");
}

TEST_CASE(ieee_crc32_matches_standard_vector_and_chunked_streaming) {
    const uint8_t vec[] = "123456789";
    uint32_t single = crc32Update(0, vec, 9);
    ASSERT_EQ(single, 0xCBF43926UL);

    uint32_t streamed = 0;
    streamed = crc32Update(streamed, vec, 4);
    streamed = crc32Update(streamed, vec + 4, 5);
    ASSERT_EQ(streamed, 0xCBF43926UL);
}

TEST_CASE(obd2_dtc_decoder_all_four_families) {
    ASSERT_STREQ(decodeObdDtc(0x0300).c_str(), "P0300");
    ASSERT_STREQ(decodeObdDtc(0x5234).c_str(), "C1234");
    ASSERT_STREQ(decodeObdDtc(0x8001).c_str(), "B0001");
    ASSERT_STREQ(decodeObdDtc(0xC100).c_str(), "U0100");
}

TEST_CASE(logic_analyzer_uart_8n1_edge_decoder) {
    // Synthesize 8N1 UART byte 'A' (0x41 = 0b01000001 LSB first) at 10 ticks/bit
    // Idle=1, Start(0)=[10..20], b0(1)=[20..30], b1..b5(0)=[30..80], b6(1)=[80..90], b7(0)=[90..100], Stop(1)=[100..]
    std::vector<uint32_t> edges = {
        (10U << 1) | 0U, // start bit falling
        (20U << 1) | 1U, // b0 = 1
        (30U << 1) | 0U, // b1..b5 = 0
        (80U << 1) | 1U, // b6 = 1
        (90U << 1) | 0U, // b7 = 0
        (100U << 1) | 1U // stop bit = 1
    };
    const uint64_t bitTicks = 10;
    uint8_t decoded = 0;
    for (int bit = 0; bit < 8; ++bit) {
        uint64_t t = 10 + (uint64_t)((bit + 1.5) * (double)bitTicks);
        if (sampleEdgeAt(1, edges, t)) decoded |= (1U << bit);
    }
    uint8_t stopBit = sampleEdgeAt(1, edges, 10 + (uint64_t)(9.5 * (double)bitTicks));
    ASSERT_EQ(decoded, static_cast<uint8_t>('A'));
    ASSERT_EQ(stopBit, static_cast<uint8_t>(1));
}

TEST_CASE(logic_analyzer_onewire_reset_and_read_rom_byte) {
    // 1 MHz resolution (1 tick = 1 us): Reset low 480us, then byte 0x33 (00110011 LSB-first)
    std::vector<uint32_t> edges;
    uint32_t t = 100;
    edges.push_back((t << 1) | 0);
    t += 480; // Reset pulse
    edges.push_back((t << 1) | 1);
    t += 200;

    uint8_t romCmd = 0x33;
    for (int b = 0; b < 8; ++b) {
        uint32_t lowUs = ((romCmd >> b) & 1) ? 6 : 60;
        edges.push_back((t << 1) | 0);
        t += lowUs;
        edges.push_back((t << 1) | 1);
        t += 70;
    }

    bool sawReset = false;
    uint8_t outByte = 0;
    int bitCnt = 0;
    for (size_t i = 0; i + 1 < edges.size(); ++i) {
        if ((edges[i] & 1) == 0 && (edges[i + 1] & 1) == 1) {
            uint32_t lowUs = (edges[i + 1] >> 1) - (edges[i] >> 1);
            if (lowUs >= 380) sawReset = true;
            else {
                uint8_t bit = (lowUs < 25) ? 1 : 0;
                outByte |= static_cast<uint8_t>(bit << bitCnt++);
            }
        }
    }
    ASSERT_TRUE(sawReset);
    ASSERT_EQ(bitCnt, 8);
    ASSERT_EQ(outByte, static_cast<uint8_t>(0x33));
}

int main() { return nezha_test::runAll("UART ANSI, CRC32, OBD-II & Logic Decoders"); }
