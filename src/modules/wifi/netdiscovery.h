#ifndef __NETDISCOVERY_H__
#define __NETDISCOVERY_H__

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

/*
 * LAN discovery browser: enumerates mDNS service types/instances and SSDP/UPnP responders, merging
 * both into one bounded device table. The TFT menu entry and the headless `mdns` serial command
 * drive the same engine, so the two paths can never disagree about what "found" means.
 *
 * Packet decoding is delegated to the shared DNS codec (modules/wifi/dns_wire.h); this module only
 * owns the multicast socket plumbing, the fixed-size record table and the two presentation layers.
 */

#include <stddef.h>
#include <stdint.h>

// Fixed caps. Every table below is a POD array sized at compile time: nothing grows while datagrams
// arrive, so a chatty LAN can never fragment the heap in the middle of a scan.
static constexpr size_t ND_MAX_DEVICES = 64;       // merged mDNS + SSDP rows
static constexpr size_t ND_MAX_MDNS_DEVICES = 48;  // mDNS share of the merged table
static constexpr size_t ND_MAX_SERVICE_TYPES = 12; // _services._dns-sd._udp.local answers kept

// Why a scan refused to run. Completed means the window elapsed (results may still be empty).
enum class NdOutcome : uint8_t { Completed, Cancelled, NoSocket, LowRam, NotConnected, NoMemory };

// One discovered host, always NUL-terminated and never overgrown by the parsers. ip is IPv4
// (AAAA answers are ignored: the table has no room for a 16-byte address); port is 0 when the
// responder never answered SRV.
struct NdDevice {
    char name[64];
    char type[32];
    char info[48];
    uint8_t ip[4];
    uint16_t port;
    bool fromMdns;
};

// Invoked once per discovered device after the scan window closed, so ip/port are fully resolved.
// type/name are never null; ip is always the four-byte address that was stored in the table.
typedef void (*NdScanCallback)(
    const char *type, const char *name, const uint8_t ip[4], uint16_t port, void *ctx
);

// Interactive browser: pre-flight guards, live scan screen, then a scrollable result list.
// UP/DN scroll, NEXT (or SEL) writes /netdiscovery.txt, ESC leaves. WiFi STA stays connected.
void netdiscovery_run();

// Headless engine for the serial CLI and the WebUI: never draws, never polls keys. windowMs is
// clamped to 2000..60000. Returns false when the pre-flight guard refused to run (low contiguous
// DMA, no STA link) or a multicast socket could not be opened; true once the scan window elapsed,
// even when nothing answered. The WebUI is stopped first because it owns ESPmDNS on port 5353.
// When outcome is non-null it receives the precise refusal reason (or Completed/Cancelled).
bool netdiscovery_scan(uint16_t windowMs, NdScanCallback cb, void *ctx, NdOutcome *outcome = nullptr);

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT

#endif // __NETDISCOVERY_H__
