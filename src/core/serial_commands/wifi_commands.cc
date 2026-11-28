#include "wifi_commands.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h" //to return MAC addr
#include <globals.h>
#if !defined(LITE_VERSION)
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "modules/wifi/sniffer.h"
#include "modules/wifi/tcp_utils.h"
#include <modules/ethernet/ARPScanner.h>
// #include "modules/wifi/responder.h"
#endif
#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "modules/wifi/dnstools.h"
#include "modules/wifi/netdiscovery.h"
#endif
uint32_t wifiCallback(cmd *c) {
    Command cmd(c);
    Argument statusArg = cmd.getArgument("status");
    String status = statusArg.getValue();
    status.trim();

    Argument ssidArg = cmd.getArgument("ssid");
    String ssid = ssidArg.getValue();
    ssid.trim();

    Argument pwdArg = cmd.getArgument("pwd");
    String pwd = pwdArg.getValue();
    pwd.trim();

    if (status == "off") {
        wifiDisconnect();
        return true;
    } else if (status == "on") {
        if (wifiConnected) {
            serialDevice->println("Wifi already connected");
            return true;
        }
        if (wifiConnecttoKnownNet()) return true;
        wifiDisconnect();
        return _setupAP();

    } else if (status == "add" && ssid != "" && pwd != "") {
        bruceConfig.addWifiCredential(ssid, pwd);
        return true;
    } else {
        serialDevice->println(
            "Invalid status: " + status +
            "\n"
            "Possible commands: \n"
            "-> wifi off (Disconnects Wifi)\n"
            "-> wifi on  (Connects to a known Wifi network. if there's no known network, starts in AP Mode)\n"
            "-> wifi add SSID Password (adds a network to the list)"
        );
        return false;
    }
}

uint32_t webuiCallback(cmd *c) {
    Command cmd(c);

    Argument arg = cmd.getArgument("noAp");
    bool noAp = arg.isSet();

    serialDevice->println(String("Starting Web UI ") + !noAp ? "AP" : "STA");
    serialDevice->println("Press ESC to quit");
    startWebUi(!noAp); // MEMO: will quit when check(EscPress)

    return true;
}
#if !defined(LITE_VERSION)
uint32_t scanHostsCallback(cmd *c) {
    esp_netif_t *esp_netinterface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (esp_netinterface == nullptr) {
        Serial.println("Failed to get netif handle\nTry connecting to a network first");
        return false;
    }

    ARPScanner{esp_netinterface};

    return true;
}

uint32_t snifferCallback(cmd *c) {
    sniffer_setup();

    return true;
}

uint32_t listenTCPCallback(cmd *c) {
    if (!wifiConnected) {
        Serial.println("Connect to a WiFi first.");
        return false;
    }

    listenTcpPort();

    return true;
}
#endif
/*
uint32_t responderCallback(cmd *c) {
    if (!wifiConnected) {
        Serial.println("Connect to a WiFi first.");
        return false;
    }

    responder();

    return true;
}
*/

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
uint32_t mdnsCallback(cmd *c) {
    Command cmd(c);
    Argument secsArg = cmd.getArgument("secs");
    String secsStr = secsArg.getValue();
    secsStr.trim();
    long secs = secsStr.length() ? secsStr.toInt() : 6; // default 6 s window
    if (secs < 2) secs = 2;
    if (secs > 60) secs = 60;

    serialDevice->println("Scanning mDNS + SSDP...");
    NdOutcome outcome = NdOutcome::Completed;
    const bool ok = netdiscovery_scan(
        (uint16_t)(secs * 1000),
        [](const char *type, const char *name, const uint8_t ip[4], uint16_t port, void *) {
            serialDevice->printf("%s|%s|%u.%u.%u.%u|%u\n", type, name, ip[0], ip[1], ip[2], ip[3], port);
        },
        nullptr,
        &outcome
    );
    if (!ok) {
        switch (outcome) {
            case NdOutcome::LowRam: serialDevice->println("Low RAM: free BLE/SD"); break;
            case NdOutcome::NotConnected: serialDevice->println("Connect to WiFi first"); break;
            case NdOutcome::NoSocket: serialDevice->println("Cannot open multicast socket"); break;
            case NdOutcome::NoMemory: serialDevice->println("Out of memory"); break;
            case NdOutcome::Cancelled: serialDevice->println("Scan cancelled"); break;
            default: serialDevice->println("Scan failed"); break;
        }
        return false;
    }
    return true;
}

uint32_t dnsCallback(cmd *c) {
    Command cmd(c);
    Argument nameArg = cmd.getArgument("name");
    String name = nameArg.getValue();
    name.trim();
    if (!name.length()) {
        serialDevice->println("Usage: dns <domain> [type] | dns reverse <ip> | dns brute <domain> [count]");
        return false;
    }
    Argument typeArg = cmd.getArgument("type");
    String type = typeArg.getValue();
    type.trim();
    Argument arg2 = cmd.getArgument("arg2");
    String arg2Str = arg2.getValue();
    arg2Str.trim();

    char out[1024];
    if (name.equalsIgnoreCase("reverse")) {
        char arpa[64];
        if (!dnstools_reverse(type.c_str(), arpa, sizeof(arpa))) {
            serialDevice->println("Invalid IPv4 address");
            return false;
        }
        const bool ok = dnstools_query(arpa, dns_wire::TYPE_PTR, out, sizeof(out));
        serialDevice->println(out);
        return ok;
    }
    if (name.equalsIgnoreCase("brute")) {
        // Headless brute: domain comes from the positional argument, never the TFT keyboard.
        if (!type.length()) {
            serialDevice->println("Usage: dns brute <domain> [count]");
            return false;
        }
        long n = arg2Str.length() ? arg2Str.toInt() : 1000;
        if (n < 1 || n > 1000) {
            serialDevice->println("Count must be 1..1000");
            return false;
        }
        serialDevice->printf("Brute forcing %s (up to %ld queries)...\n", type.c_str(), n);
        const bool ok = dnstools_brute_headless(
            type.c_str(), "/wordlists/subdomains.txt", (uint16_t)n, nullptr, nullptr
        );
        serialDevice->println(ok ? "Brute force finished" : "Brute force aborted");
        return ok;
    }
    uint16_t qtype = dns_wire::TYPE_A;
    if (type.length()) {
        qtype = dnstools_type_from_name(type.c_str());
        if (qtype == 0) {
            serialDevice->println("Unknown record type: " + type);
            return false;
        }
    }
    const bool ok = dnstools_query(name.c_str(), qtype, out, sizeof(out));
    serialDevice->println(out);
    return ok;
}
#endif

void createWifiCommands(SimpleCLI *cli) {
    Command webuiCmd = cli->addCommand("webui", webuiCallback);
    webuiCmd.addFlagArg("noAp");

    Command wifiCmd = cli->addCommand("wifi", wifiCallback);
    wifiCmd.addPosArg("status");
    wifiCmd.addPosArg("ssid", "");
    wifiCmd.addPosArg("pwd", "");

#if !defined(LITE_VERSION)

    Command ScanHostsCmd = cli->addCommand("arp", scanHostsCallback);

    Command listenTCPCmd =
        cli->addCommand("listen", listenTCPCallback); // TODO: make possible to select port to open via Serial

    Command snifferCmd =
        cli->addCommand("sniffer", snifferCallback); // TODO: be able to exit from it from Serial

#endif
#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

    Command mdnsCmd = cli->addCommand("mdns", mdnsCallback);
    mdnsCmd.addPosArg("secs", "");

    Command dnsCmd = cli->addCommand("dns", dnsCallback);
    dnsCmd.addPosArg("name");
    dnsCmd.addPosArg("type", "");
    dnsCmd.addPosArg("arg2", "");

#endif
    // Command responderCmd = cli->addCommand("responder", responderCallback); TODO
}
