#include "net_utils.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <sstream>

bool internetConnection() {
    WiFiClient client;
    const char *host = "8.8.8.8"; // Google DNS
    const uint16_t port = 53;     // DNS Port

    if (client.connect(host, port)) {
        client.stop();
        return true; // Connected
    } else return false;
}

String getManufacturer(const String &mac) {
    if (!internetConnection()) { return "NO_INTERNET_ACCESS"; }

    // there is an official(IEEE) doc that contains all registered mac prefixes
    // but it is around 700kb and i don't know a way to get specific part
    // without downloading the whole txt
    HTTPClient http;
    http.begin("http://api.maclookup.app/v2/macs/" + mac);
    int httpCode = http.GET(); // Send the request
    if (httpCode != 200) {
        http.end();
        return "GET failed";
    }

    // payload is a json of the format
    // {"success":true,"found":true,"macPrefix":"2C3358","company":"Intel Corporate","address":"Lot 8, Jalan
    // Hi-Tech 2/3, Kulim Kedah 09000,
    // MY","country":"MY","blockStart":"2C3358000000","blockEnd":"2C3358FFFFFF","blockSize":16777215,"blockType":"MA-L","updated":"2021-10-13","isRand":false,"isPrivate":false}
    // company field is going to be empty if none found
    String payload{http.getString()};
    size_t company_start_idx = payload.indexOf("company") + 10; // + 7(company) + 3(":")
    String manufacturer = payload.substring(company_start_idx, payload.indexOf('"', company_start_idx));
    if (manufacturer.isEmpty()) return "UNKNOWN";

    return manufacturer;
}

String MAC(uint8_t *data) {
    char macStr[18];
    snprintf(
        macStr,
        sizeof(macStr),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        data[0],
        data[1],
        data[2],
        data[3],
        data[4],
        data[5]
    );

    return macStr;
}

void stringToMAC(const std::string &macStr, uint8_t MAC[6]) {
    memset(MAC, 0, 6);
    std::stringstream ss(macStr);
    unsigned int temp;
    for (int i = 0; i < 6; ++i) {
        char delimiter;
        if (!(ss >> std::hex >> temp)) return;
        MAC[i] = static_cast<uint8_t>(temp);
        ss >> delimiter;
    }
}

bool stringToMACChecked(const String &macStr, uint8_t MAC[6]) {
    memset(MAC, 0, 6);
    if (macStr.length() < 11) return false;
    unsigned int b[6] = {0};
    int matched = sscanf(
        macStr.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]
    );
    if (matched != 6) {
        matched = sscanf(
            macStr.c_str(), "%2x-%2x-%2x-%2x-%2x-%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]
        );
    }
    if (matched != 6) return false;
    bool allZero = true;
    for (int i = 0; i < 6; ++i) {
        if (b[i] > 0xFF) return false;
        MAC[i] = static_cast<uint8_t>(b[i]);
        if (MAC[i] != 0) allZero = false;
    }
    if (allZero) return false;
    if (MAC[0] & 0x01) return false; // multicast/broadcast
    return true;
}

// Função para converter IP para string
String ipToString(const uint8_t *ip) {
    return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// Função para converter MAC para string
String macToString(const uint8_t *mac) {
    char buf[18];
    snprintf(
        buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    return String(buf);
}
