#include "Arduino.h"
#include "unity_lite.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool parseMacBytes(const std::string &macStr, uint8_t macArray[6]) {
    const char *p = macStr.c_str();
    int byteCount = 0;
    while (*p != '\0') {
        if (byteCount >= 6) return false;
        char *end = nullptr;
        unsigned long val = std::strtoul(p, &end, 16);
        if (end == p || val > 0xFF) return false;
        macArray[byteCount++] = static_cast<uint8_t>(val);
        if (*end == '\0') break;
        if (*end != ':' && *end != '-') return false;
        p = end + 1;
        if (*p == '\0') return false;
    }
    return byteCount == 6;
}

static String formatIp(const uint8_t ip[4]) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return String(buf);
}

static const char *SAMSUNG_MAC_OUIS[] = {
    "00:1E:DF", "00:23:E7", "00:24:FE", "00:26:5C", "00:27:14", "00:2A:10", "00:2D:0A",
    "00:30:FA", "00:35:FE", "00:3C:E4", "00:40:96", "00:44:01", "00:4A:77", "00:4D:4A",
    "00:50:F7", "00:54:08", "00:57:7A", "00:5A:38", "00:5E:88", "00:62:6E", "00:64:22",
    "00:66:44", "00:68:EB", "00:6A:94", "00:6C:F0", "00:6E:2A", "00:70:89", "00:72:44",
    "00:74:04", "00:76:5E", "00:78:2C", "00:7A:04", "00:7C:2E", "00:7E:58", "00:80:82",
};
static constexpr int SAMSUNG_COUNT = sizeof(SAMSUNG_MAC_OUIS) / sizeof(SAMSUNG_MAC_OUIS[0]);

static int compareOuiPrefix8(const char *mac, const char *oui) {
    for (int i = 0; i < 8; ++i) {
        unsigned char c = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(mac[i])));
        unsigned char o = static_cast<unsigned char>(oui[i]);
        if (c != o) return (c < o) ? -1 : 1;
    }
    return 0;
}

static bool isSamsungMac(const char *mac) {
    if (!mac) return false;
    for (int i = 0; i < 8; ++i) {
        if (mac[i] == '\0') return false;
    }
    int lo = 0, hi = SAMSUNG_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = compareOuiPrefix8(mac, SAMSUNG_MAC_OUIS[mid]);
        if (cmp == 0) return true;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return false;
}

static void splitFilename(const String &filename, String &outName, String &outExt) {
    int extIndex = filename.lastIndexOf('.');
    outName = (extIndex >= 0) ? filename.substring(0, extIndex) : filename;
    outExt = (extIndex >= 0) ? filename.substring(extIndex) : "";
}

TEST_CASE(mac_parser_accepts_colon_and_dash_and_rejects_malformed) {
    uint8_t out[6] = {};
    uint8_t exp1[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23};
    ASSERT_TRUE(parseMacBytes("DE:AD:BE:EF:01:23", out));
    ASSERT_MEMEQ(out, exp1, 6);

    ASSERT_TRUE(parseMacBytes("de-ad-be-ef-01-23", out));
    ASSERT_MEMEQ(out, exp1, 6);

    ASSERT_FALSE(parseMacBytes("DE:AD:BE:EF:01", out));       // too short
    ASSERT_FALSE(parseMacBytes("DE:AD:BE:EF:01:23:45", out)); // too long
    ASSERT_FALSE(parseMacBytes("DE:AD:BE:EF:01:", out));      // trailing delim
    ASSERT_FALSE(parseMacBytes("1FF:00:00:00:00:00", out));   // > 0xFF
    ASSERT_FALSE(parseMacBytes("ZZ:00:00:00:00:00", out));    // non-hex
    ASSERT_FALSE(parseMacBytes("", out));
}

TEST_CASE(ip_formatting) {
    uint8_t ip1[4] = {192, 168, 4, 1};
    uint8_t ip2[4] = {0, 0, 0, 0};
    uint8_t ip3[4] = {255, 255, 255, 255};
    ASSERT_STREQ(formatIp(ip1).c_str(), "192.168.4.1");
    ASSERT_STREQ(formatIp(ip2).c_str(), "0.0.0.0");
    ASSERT_STREQ(formatIp(ip3).c_str(), "255.255.255.255");
}

TEST_CASE(samsung_oui_binary_search_all_entries_and_edges) {
    for (int i = 0; i < SAMSUNG_COUNT; ++i) {
        std::string full = std::string(SAMSUNG_MAC_OUIS[i]) + ":11:22:33";
        ASSERT_TRUE(isSamsungMac(full.c_str()));
    }
    ASSERT_TRUE(isSamsungMac("00:1e:df:aa:bb:cc"));  // lowercase first entry
    ASSERT_TRUE(isSamsungMac("00:80:82:ff:ff:ff"));  // last entry
    ASSERT_FALSE(isSamsungMac("00:1A:2B:11:22:33")); // non-Samsung
    ASSERT_FALSE(isSamsungMac("00:1E"));             // too short
    ASSERT_FALSE(isSamsungMac(""));
    ASSERT_FALSE(isSamsungMac(nullptr));
}

TEST_CASE(create_new_file_extensionless_and_multi_dot) {
    String n, e;
    splitFilename("capture.sub", n, e);
    ASSERT_STREQ(n.c_str(), "capture");
    ASSERT_STREQ(e.c_str(), ".sub");

    splitFilename("capture_no_ext", n, e);
    ASSERT_STREQ(n.c_str(), "capture_no_ext");
    ASSERT_STREQ(e.c_str(), "");

    splitFilename("a.b.pcap", n, e);
    ASSERT_STREQ(n.c_str(), "a.b");
    ASSERT_STREQ(e.c_str(), ".pcap");
}

int main() { return nezha_test::runAll("Network, OUI Binary Search & Path Helpers"); }
