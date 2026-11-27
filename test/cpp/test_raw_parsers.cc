#include "unity_lite.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static size_t parseIrRawTimings(const char *rawPtr, uint16_t *out, size_t maxCount) {
    size_t count = 0;
    const char *p = rawPtr;
    while (*p != '\0' && count < maxCount) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        char *end = nullptr;
        unsigned long val = std::strtoul(p, &end, 10);
        if (end == p) {
            while (*p != '\0' && *p != ' ') ++p;
            continue;
        }
        out[count++] = static_cast<uint16_t>(val);
        p = end;
    }
    return count;
}

static size_t parseRfRawTimings(const char *rawPtr, int *out, size_t maxCount) {
    size_t count = 0;
    const char *p = rawPtr;
    while (*p != '\0' && count < maxCount) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        char *end = nullptr;
        long val = std::strtol(p, &end, 10);
        if (end == p) {
            while (*p != '\0' && *p != ' ') ++p;
            continue;
        }
        out[count++] = static_cast<int>(val);
        p = end;
    }
    out[count] = 0;
    return count;
}

// Memory-backed WordlistReader matching wifi_recover.cc overlong-line safety fix
struct MemWordlistReader {
    std::string owned;
    const uint8_t *src;
    size_t srcLen, srcOff;
    uint8_t buf[64];
    size_t cap = 64, len = 0, pos = 0;
    bool eof = false;

    MemWordlistReader(std::string data)
        : owned(std::move(data)), src(reinterpret_cast<const uint8_t *>(owned.data())), srcLen(owned.size()),
          srcOff(0) {}

    void refill() {
        if (eof) return;
        size_t rem = len - pos;
        if (rem) std::memmove(buf, buf + pos, rem);
        len = rem;
        pos = 0;
        size_t space = cap - len;
        if (!space) return;
        size_t avail = srcLen - srcOff;
        size_t rd = (avail < space) ? avail : space;
        if (rd > 0) {
            std::memcpy(buf + len, src + srcOff, rd);
            srcOff += rd;
            len += rd;
        } else {
            eof = true;
        }
    }

    bool nextLine(char *out, size_t maxLen) {
        while (true) {
            for (size_t i = pos; i < len; ++i) {
                if (buf[i] == '\n') {
                    size_t ll = i - pos;
                    if (ll > 0 && buf[pos + ll - 1] == '\r') ll--;
                    size_t cp = (ll < maxLen) ? ll : maxLen;
                    std::memcpy(out, buf + pos, cp);
                    out[cp] = '\0';
                    pos = i + 1;
                    return true;
                }
            }
            if (eof) {
                size_t rem = len - pos;
                if (!rem) return false;
                if (buf[pos + rem - 1] == '\r') rem--;
                size_t cp = (rem < maxLen) ? rem : maxLen;
                std::memcpy(out, buf + pos, cp);
                out[cp] = '\0';
                pos = len;
                return cp > 0;
            }
            if (pos == 0 && len == cap) {
                size_t cp = (len < maxLen) ? len : maxLen;
                std::memcpy(out, buf, cp);
                out[cp] = '\0';
                pos = len;
                return cp > 0;
            }
            refill();
        }
    }
};

TEST_CASE(ir_raw_parser_handles_whitespace_and_bad_tokens) {
    uint16_t buf[16] = {};
    size_t n = parseIrRawTimings("  9000   4500\t560 bad 1690  ", buf, 16);
    ASSERT_EQ(n, (size_t)4);
    ASSERT_EQ(buf[0], (uint16_t)9000);
    ASSERT_EQ(buf[1], (uint16_t)4500);
    ASSERT_EQ(buf[2], (uint16_t)560);
    ASSERT_EQ(buf[3], (uint16_t)1690);

    ASSERT_EQ(parseIrRawTimings("   ", buf, 16), (size_t)0);
}

TEST_CASE(rf_raw_parser_handles_signed_pulses_and_sentinel) {
    int buf[16] = {};
    size_t n = parseRfRawTimings(" 500 -1000 350 -700 ", buf, 15);
    ASSERT_EQ(n, (size_t)4);
    ASSERT_EQ(buf[0], 500);
    ASSERT_EQ(buf[1], -1000);
    ASSERT_EQ(buf[2], 350);
    ASSERT_EQ(buf[3], -700);
    ASSERT_EQ(buf[4], 0); // termination sentinel
}

TEST_CASE(wordlist_reader_handles_crlf_eof_and_overlong_line_without_hang) {
    MemWordlistReader r1("pass1\r\npass2\npass3");
    char line[128];
    ASSERT_TRUE(r1.nextLine(line, sizeof(line) - 1));
    ASSERT_STREQ(line, "pass1");
    ASSERT_TRUE(r1.nextLine(line, sizeof(line) - 1));
    ASSERT_STREQ(line, "pass2");
    ASSERT_TRUE(r1.nextLine(line, sizeof(line) - 1));
    ASSERT_STREQ(line, "pass3");
    ASSERT_FALSE(r1.nextLine(line, sizeof(line) - 1));

    // Overlong line (> cap=64 bytes with no '\n') MUST NOT infinite loop
    std::string giant(200, 'A');
    MemWordlistReader r2(giant);
    int chunks = 0;
    while (r2.nextLine(line, sizeof(line) - 1)) {
        chunks++;
        ASSERT_TRUE(chunks <= 10);
    }
    ASSERT_TRUE(chunks >= 3);
}

int main() { return nezha_test::runAll("Zero-Alloc IR/RF Parsers & WordlistReader"); }
