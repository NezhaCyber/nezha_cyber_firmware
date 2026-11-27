#include "net_utils.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

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
    http.end();

    int company_key_idx = payload.indexOf("\"company\":\"");
    if (company_key_idx < 0) return "UNKNOWN";
    int company_start_idx = company_key_idx + 11; // strlen("\"company\":\"")
    int company_end_idx = payload.indexOf('"', company_start_idx);
    if (company_end_idx <= company_start_idx) return "UNKNOWN";
    String manufacturer = payload.substring(company_start_idx, company_end_idx);
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
    unsigned int b[6] = {0};
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 ||
        sscanf(macStr.c_str(), "%x-%x-%x-%x-%x-%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; ++i) { MAC[i] = static_cast<uint8_t>(b[i]); }
    }
}

// Função para converter IP para string
String ipToString(const uint8_t *ip) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return String(buf);
}

// Função para converter MAC para string
String macToString(const uint8_t *mac) {
    char buf[18];
    snprintf(
        buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    return String(buf);
}
