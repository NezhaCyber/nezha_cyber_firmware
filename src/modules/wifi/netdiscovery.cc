#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "netdiscovery.h"

#include "core/display.h"
#include "core/i18n.h"
#include "core/logging/nzlog.h"
#include "core/radio_mem.h"    // radioHasMemForWifi/radioLowRamError/cleanlyStopWebUiForWiFiFeature
#include "core/sd_functions.h" // getFsStorage/sdcardMounted
#include "dns_wire.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

/*
 * Bounded mDNS/SSDP scan state, protocol timing, and fixed storage limits.
 * NdEngine owns all device/type/host tables and one 1460-byte datagram buffer;
 * it is allocated per scan in PSRAM when available and freed by every public exit.
 */

constexpr uint16_t ND_MDNS_PORT = 5353;
constexpr uint16_t ND_SSDP_PORT = 1900;
constexpr uint16_t ND_SCAN_WINDOW_MS = 5500; // UI scan window: 1.1s types, 1.65s browse, 2.75s SSDP
constexpr uint16_t ND_PKT_BUF = 1460;        // one datagram, matches the UDP stack receive size
constexpr size_t ND_MAX_FQDN = 256;          // dns_wire::MAX_NAME_TEXT
constexpr size_t ND_MAX_HOSTS = 24;          // A-record cache used to resolve SRV targets
constexpr size_t ND_ADDR_CHARS = 22;         // "255.255.255.255:65535" plus the NUL

enum : uint8_t { ND_PHASE_TYPES = 0, ND_PHASE_BROWSE = 1, ND_PHASE_SSDP = 2 };

struct NdHostAddr {
    char host[64];
    uint8_t ip[4];
};

// Single owner of the scan result. Allocated once per scan (PSRAM when present) and freed on every
// exit path, so the browser costs no permanent .bss on the 16MB PSRAM boards.
struct NdEngine {
    NdDevice devices[ND_MAX_DEVICES];
    size_t count;
    char serviceTypes[ND_MAX_SERVICE_TYPES][48];
    size_t typeCount;
    NdHostAddr hosts[ND_MAX_HOSTS];
    size_t hostCount;
    size_t hostNext; // round-robin slot once the host cache is full
    uint8_t pkt[ND_PKT_BUF];
};

typedef void (*NdTickFn)(
    uint8_t phase, size_t mdnsCount, size_t ssdpCount, size_t typeCount, uint32_t elapsedMs, uint32_t totalMs,
    void *ctx
);

struct NdTick {
    NdTickFn fn;
    void *ctx;
};

// Asked for when the _services._dns-sd._udp.local meta query stays silent (some APs filter it).
// A const array lands in flash on ESP32, so the fallback list costs no DRAM.
const char *const kFallbackServiceTypes[] = {
    "_http._tcp.local",
    "_ipp._tcp.local",
    "_printer._tcp.local",
    "_airplay._tcp.local",
    "_raop._tcp.local",
    "_googlecast._tcp.local",
    "_hap._tcp.local",
    "_smb._tcp.local",
    "_ssh._tcp.local",
    "_workstation._tcp.local",
    "_afpovertcp._tcp.local",
    "_spotify-connect._tcp.local",
};
constexpr size_t ND_FALLBACK_TYPE_COUNT = sizeof(kFallbackServiceTypes) / sizeof(kFallbackServiceTypes[0]);

// M-SEARCH is built as one literal so every line ending is an explicit \r\n (a stray LF makes some
// UPnP stacks ignore the datagram). MAN must keep its literal quotes.
const char kMSearch[] = "M-SEARCH * HTTP/1.1\r\n"
                        "HOST: 239.255.255.250:1900\r\n"
                        "MAN: \"ssdp:discover\"\r\n"
                        "MX: 2\r\n"
                        "ST: ssdp:all\r\n"
                        "\r\n";

/*
 * Small string helpers always terminate bounded outputs, trim protocol whitespace,
 * compare ASCII case-insensitively, and split service instances into fixed label
 * and type buffers without allocating.
 */

void ndCopyStr(char *dst, size_t cap, const char *src, size_t len) {
    if (dst == nullptr || cap == 0) return;
    if (src == nullptr) len = 0;
    if (len >= cap) len = cap - 1;
    if (len != 0) memcpy(dst, src, len);
    dst[len] = '\0';
}

void ndCopyCStr(char *dst, size_t cap, const char *src) {
    ndCopyStr(dst, cap, src, src != nullptr ? strlen(src) : 0);
}

size_t ndTrimRight(const char *s, size_t len) {
    while (len != 0) {
        const char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --len;
    }
    return len;
}

bool ndStartsWithCI(const char *s, const char *prefix) {
    if (s == nullptr || prefix == nullptr) return false;
    for (; *prefix != '\0'; ++s, ++prefix) {
        char a = *s;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// DNS names are case-insensitive (RFC 4343/RFC 6762 section 16): dedupe and host lookups must
// not create a second row because a responder changed case between packets.
bool ndEqualsCI(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) return a == b;
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == *b;
}

// Network-supplied strings land on the TFT, the serial console and the saved log. Keep only
// printable bytes and replace the field separator '|' (plus whitespace controls) with '?' so a
// hostile SSDP/SERVER/TXT value cannot forge result rows or inject terminal control sequences.
void ndSanitizeCopy(char *dst, size_t cap, const char *src) {
    size_t used = 0;
    if (src == nullptr) src = "";
    for (; *src != '\0' && used + 1 < cap; ++src) {
        const uint8_t c = (uint8_t)*src;
        if (c < 0x20 || c > 0x7E || c == '|') {
            dst[used++] = '?';
        } else {
            dst[used++] = (char)c;
        }
    }
    if (cap > 0) dst[used] = '\0';
}

bool ndIpSet(const uint8_t ip[4]) { return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0; }

// "Kitchen Speaker._airplay._tcp.local" -> label "Kitchen Speaker", type "_airplay._tcp".
// A bare service type ("_http._tcp.local") yields an empty label and type "_http._tcp".
// parseName() renders an escaped dot as "\046", so splitting on '.' can never split a label.
void ndSplitInstance(const char *fqdn, char *label, size_t labelCap, char *type, size_t typeCap) {
    label[0] = '\0';
    type[0] = '\0';
    if (fqdn == nullptr || fqdn[0] == '\0' || (fqdn[0] == '.' && fqdn[1] == '\0')) return;

    const char *dot1 = strrchr(fqdn, '.'); // before "local"
    if (dot1 == nullptr) {
        ndCopyCStr(type, typeCap, fqdn);
        return;
    }
    const char *dot2 = nullptr; // before "_tcp"
    for (const char *p = fqdn; p < dot1; ++p) {
        if (*p == '.') dot2 = p;
    }
    if (dot2 == nullptr) {
        ndCopyStr(type, typeCap, fqdn, (size_t)(dot1 - fqdn));
        return;
    }
    const char *dot3 = nullptr; // closes the instance label
    for (const char *p = fqdn; p < dot2; ++p) {
        if (*p == '.') dot3 = p;
    }
    if (dot3 == nullptr) {
        ndCopyStr(type, typeCap, fqdn, (size_t)(dot1 - fqdn));
        return;
    }
    ndCopyStr(label, labelCap, fqdn, (size_t)(dot3 - fqdn));
    ndCopyStr(type, typeCap, dot3 + 1, (size_t)(dot1 - (dot3 + 1)));
}

/*
 * Merge discovered services into fixed device, service-type, and A-record host
 * tables without reallocation. mDNS rows merge by identity or SRV address/port;
 * SSDP rows share one address/port identity, and full tables reject new entries
 * except for the round-robin host cache.
 */

struct DeviceCounts {
    size_t mdns;
    size_t ssdp;
};

DeviceCounts ndCounts(const NdEngine &e) {
    DeviceCounts c = {0, 0};
    for (size_t i = 0; i < e.count; ++i) {
        if (e.devices[i].fromMdns) ++c.mdns;
        else ++c.ssdp;
    }
    return c;
}

NdEngine *ndEngineAlloc() {
    return static_cast<NdEngine *>(
        psramFound() ? ps_calloc(1, sizeof(NdEngine)) : calloc(1, sizeof(NdEngine))
    );
}

int ndFindMdns(const NdEngine &e, const char *name, const char *type, uint16_t port, const uint8_t ip[4]) {
    if (name != nullptr && name[0] != '\0') {
        for (size_t i = 0; i < e.count; ++i) {
            const NdDevice &d = e.devices[i];
            if (!d.fromMdns) continue;
            if (type != nullptr && type[0] != '\0' && !ndEqualsCI(d.type, type)) continue;
            if (ndEqualsCI(d.name, name)) return (int)i;
        }
    }
    // An SRV that arrives before its PTR still lands on the right row.
    if (port != 0 && ndIpSet(ip)) {
        for (size_t i = 0; i < e.count; ++i) {
            const NdDevice &d = e.devices[i];
            if (d.fromMdns && d.port == port && memcmp(d.ip, ip, 4) == 0) return (int)i;
        }
    }
    return -1;
}

int ndUpsertMdns(NdEngine &e, const char *name, const char *type, const uint8_t ip[4], uint16_t port) {
    const int found = ndFindMdns(e, name, type, port, ip);
    if (found >= 0) {
        NdDevice &d = e.devices[found];
        if (port != 0) d.port = port;
        if (ndIpSet(ip)) memcpy(d.ip, ip, 4);
        return found;
    }
    if (e.count >= ND_MAX_DEVICES) return -1;
    if (ndCounts(e).mdns >= ND_MAX_MDNS_DEVICES) return -1;

    NdDevice &d = e.devices[e.count];
    memset(&d, 0, sizeof(d));
    ndSanitizeCopy(d.name, sizeof(d.name), name);
    ndSanitizeCopy(d.type, sizeof(d.type), type);
    if (ndIpSet(ip)) memcpy(d.ip, ip, 4);
    d.port = port;
    d.fromMdns = true;
    return (int)e.count++;
}

int ndUpsertSsdp(
    NdEngine &e, const char *name, const char *type, const uint8_t ip[4], uint16_t port, const char *info
) {
    // One appliance answers once per ST (rootdevice, uuid, device, service...) from the same
    // address, so the address pair is the identity here - the SERVER banner is not unique.
    for (size_t i = 0; i < e.count; ++i) {
        const NdDevice &d = e.devices[i];
        if (!d.fromMdns && d.port == port && memcmp(d.ip, ip, 4) == 0) return (int)i;
    }
    if (e.count >= ND_MAX_DEVICES) return -1;

    NdDevice &d = e.devices[e.count];
    memset(&d, 0, sizeof(d));
    ndSanitizeCopy(d.name, sizeof(d.name), name);
    ndSanitizeCopy(d.type, sizeof(d.type), type);
    ndSanitizeCopy(d.info, sizeof(d.info), info);
    if (ndIpSet(ip)) memcpy(d.ip, ip, 4);
    d.port = port;
    d.fromMdns = false;
    return (int)e.count++;
}

void ndHostPut(NdEngine &e, const char *host, const uint8_t ip[4]) {
    if (host == nullptr || host[0] == '\0') return;
    for (size_t i = 0; i < e.hostCount; ++i) {
        if (ndEqualsCI(e.hosts[i].host, host)) {
            memcpy(e.hosts[i].ip, ip, 4);
            return;
        }
    }
    size_t slot;
    if (e.hostCount < ND_MAX_HOSTS) slot = e.hostCount++;
    else slot = e.hostNext++ % ND_MAX_HOSTS;
    ndCopyCStr(e.hosts[slot].host, sizeof(e.hosts[slot].host), host);
    memcpy(e.hosts[slot].ip, ip, 4);
}

bool ndHostLookup(const NdEngine &e, const char *host, uint8_t out[4]) {
    for (size_t i = 0; i < e.hostCount; ++i) {
        if (ndEqualsCI(e.hosts[i].host, host)) {
            memcpy(out, e.hosts[i].ip, 4);
            return true;
        }
    }
    return false;
}

void ndAddServiceType(NdEngine &e, const char *name) {
    if (name == nullptr || name[0] == '\0' || name[0] == '.') return;
    if (ndStartsWithCI(name, "_services._dns-sd._udp")) return;
    for (size_t i = 0; i < e.typeCount; ++i) {
        if (ndEqualsCI(e.serviceTypes[i], name)) return;
    }
    if (e.typeCount >= ND_MAX_SERVICE_TYPES) return;
    ndCopyCStr(e.serviceTypes[e.typeCount], sizeof(e.serviceTypes[e.typeCount]), name);
    ++e.typeCount;
}

/*
 * Parse one mDNS response in two bounded dns_wire walks: cache A records first,
 * then consume PTR/SRV/TXT records. SRV targets use the cached host address when
 * known and the responder address otherwise; malformed names or records are ignored.
 */

struct MdnsCtx {
    NdEngine *e;
    const uint8_t *pkt;
    size_t len;
    uint8_t from[4];
};

void ndWalkSections(MdnsCtx *c, size_t firstRecordOff, dns_wire::RecordCb cb) {
    size_t off = firstRecordOff;
    const uint16_t an = dns_wire::answerCount(c->pkt, c->len);
    const uint16_t ns = dns_wire::authorityCount(c->pkt, c->len);
    const uint16_t ar = dns_wire::additionalCount(c->pkt, c->len);
    dns_wire::parseRecords(c->pkt, c->len, &off, an, cb, c);
    dns_wire::parseRecords(c->pkt, c->len, &off, ns, cb, c);
    dns_wire::parseRecords(c->pkt, c->len, &off, ar, cb, c);
}

void ndMdnsHostCb(
    uint16_t rtype, uint16_t, uint32_t, const uint8_t *rdata, uint16_t rdlen, const char *name, void *vctx
) {
    if (rtype != dns_wire::TYPE_A || rdlen != 4 || rdata == nullptr) return;
    MdnsCtx *c = static_cast<MdnsCtx *>(vctx);
    ndHostPut(*c->e, name, rdata);
}

void ndMdnsRecordCb(
    uint16_t rtype, uint16_t, uint32_t, const uint8_t *rdata, uint16_t rdlen, const char *owner, void *vctx
) {
    MdnsCtx *c = static_cast<MdnsCtx *>(vctx);
    NdEngine &e = *c->e;
    const size_t rdataOff = (size_t)(rdata - c->pkt);

    if (rtype == dns_wire::TYPE_PTR) {
        char target[ND_MAX_FQDN];
        size_t off = rdataOff;
        if (!dns_wire::parseName(c->pkt, c->len, &off, target, sizeof(target))) return;
        if (ndStartsWithCI(owner, "_services._dns-sd._udp")) {
            ndAddServiceType(e, target);
            return;
        }
        char label[64];
        char type[32];
        ndSplitInstance(target, label, sizeof(label), type, sizeof(type));
        if (type[0] == '\0') ndSplitInstance(owner, label, sizeof(label), type, sizeof(type));
        if (label[0] == '\0' && type[0] == '\0') return;
        ndUpsertMdns(e, label, type, c->from, 0);
        return;
    }

    if (rtype == dns_wire::TYPE_SRV) {
        uint16_t port = 0;
        char target[ND_MAX_FQDN];
        if (!dns_wire::parseSrv(
                c->pkt, c->len, rdataOff, rdlen, nullptr, nullptr, &port, target, sizeof(target)
            ))
            return;
        char label[64];
        char type[32];
        ndSplitInstance(owner, label, sizeof(label), type, sizeof(type));
        if (label[0] == '\0' && type[0] == '\0') return;
        uint8_t ip[4];
        const uint8_t *ipPtr = c->from; // the responder address is the fallback target address
        if (ndHostLookup(e, target, ip)) ipPtr = ip;
        ndUpsertMdns(e, label, type, ipPtr, port);
        return;
    }

    if (rtype == dns_wire::TYPE_TXT) {
        char txt[64];
        if (!dns_wire::parseTxt(rdata, rdlen, txt, sizeof(txt))) return;
        char label[64];
        char type[32];
        ndSplitInstance(owner, label, sizeof(label), type, sizeof(type));
        // An address match would need a port, which TXT does not carry: the row must already exist
        // from its PTR (normally in the same packet). Updating in place keeps the SRV-resolved IP.
        const int idx = ndFindMdns(e, label, type, 0, c->from);
        if (idx < 0) return;
        NdDevice &d = e.devices[idx];
        if (d.info[0] == '\0') ndSanitizeCopy(d.info, sizeof(d.info), txt);
    }
}

void ndHandleMdns(NdEngine &e, const uint8_t *pkt, size_t len, const uint8_t from[4]) {
    if (pkt == nullptr || len < 12) return;

    // Skip the question section: parseName() plus the 4-byte qtype/qclass pair per question.
    size_t off = 12;
    const uint16_t qd = dns_wire::questionCount(pkt, len);
    for (uint16_t i = 0; i < qd; ++i) {
        char scratch[ND_MAX_FQDN];
        if (!dns_wire::parseName(pkt, len, &off, scratch, sizeof(scratch))) return;
        if (off + 4 > len) return;
        off += 4;
    }

    MdnsCtx c;
    c.e = &e;
    c.pkt = pkt;
    c.len = len;
    memcpy(c.from, from, 4);
    ndWalkSections(&c, off, ndMdnsHostCb);   // A first: SRV targets usually follow in the same packet
    ndWalkSections(&c, off, ndMdnsRecordCb); // then the browsable records themselves
}

/*
 * Scan one SSDP 200-OK datagram as bounded CRLF-delimited headers. LOCATION,
 * ST, SERVER, and USN supply endpoint and display fields; an omitted LOCATION
 * port defaults to 443 for HTTPS and 80 otherwise. A non-IPv4 host uses the sender.
 */

// Copies one header value (leading blanks stripped, trailing CR/blank trimmed) into out.
bool ndSsdpHeader(const char *pkt, size_t len, const char *key, char *out, size_t cap) {
    if (pkt == nullptr || key == nullptr) return false;
    out[0] = '\0';
    const size_t keyLen = strlen(key);
    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end + 1 < len && !(pkt[end] == '\r' && pkt[end + 1] == '\n')) ++end;
        const bool hasCrlf = (end + 1 < len) && pkt[end] == '\r' && pkt[end + 1] == '\n';
        const size_t lineLen = (hasCrlf ? end : len) - pos;
        if (lineLen == 0) break; // blank line terminates the header block
        if (lineLen > keyLen && pkt[pos + keyLen] == ':' && ndStartsWithCI(pkt + pos, key)) {
            size_t v = pos + keyLen + 1;
            const size_t lineEnd = pos + lineLen;
            while (v < lineEnd && (pkt[v] == ' ' || pkt[v] == '\t')) ++v;
            size_t vlen = lineEnd > v ? lineEnd - v : 0;
            vlen = ndTrimRight(pkt + v, vlen);
            ndCopyStr(out, cap, pkt + v, vlen);
            return true;
        }
        if (!hasCrlf) break;
        pos = end + 2;
    }
    return false;
}

// http://192.168.1.10:49152/desc.xml -> ip 192.168.1.10, port 49152 (80, or 443 for https, when omitted).
uint16_t ndParseLocation(const char *location, uint8_t ip[4]) {
    ip[0] = ip[1] = ip[2] = ip[3] = 0;
    const char *host = location;
    const char *scheme = strstr(location, "://");
    if (scheme != nullptr) host = scheme + 3;

    const char *hostEnd = host;
    while (*hostEnd != '\0' && *hostEnd != '/' && *hostEnd != '?' && *hostEnd != ' ') ++hostEnd;
    const char *colon = nullptr;
    for (const char *p = host; p < hostEnd; ++p) {
        if (*p == ':') colon = p;
    }
    uint16_t port = ndStartsWithCI(location, "https://") ? 443 : 80;
    if (colon != nullptr) {
        uint32_t parsed = 0;
        for (const char *p = colon + 1; p < hostEnd && *p >= '0' && *p <= '9'; ++p) {
            parsed = parsed * 10 + (uint32_t)(*p - '0');
            if (parsed > 65535) {
                parsed = 65535;
                break;
            }
        }
        if (parsed != 0) port = (uint16_t)parsed;
    }

    const char *hostStop = colon != nullptr ? colon : hostEnd;
    char text[64];
    const size_t hl = (size_t)(hostStop - host);
    if (hl == 0 || hl >= sizeof(text)) return port;
    memcpy(text, host, hl);
    text[hl] = '\0';
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
        ip[0] = (uint8_t)a;
        ip[1] = (uint8_t)b;
        ip[2] = (uint8_t)c;
        ip[3] = (uint8_t)d;
    }
    return port;
}

// urn:schemas-upnp-org:device:MediaRenderer:1 -> "MediaRenderer"; upnp:rootdevice -> "rootdevice".
void ndShortSsdpType(const char *st, char *out, size_t cap) {
    out[0] = '\0';
    const char *p = st;
    if (ndStartsWithCI(p, "urn:schemas-upnp-org:")) p += 21;
    if (ndStartsWithCI(p, "upnp:")) p += 5;
    if (ndStartsWithCI(p, "device:")) p += 7;
    else if (ndStartsWithCI(p, "service:")) p += 8;
    const char *sep = strchr(p, ':');
    const size_t n = sep != nullptr ? (size_t)(sep - p) : strlen(p);
    if (n == 0) {
        ndCopyCStr(out, cap, st);
        return;
    }
    ndCopyStr(out, cap, p, n);
}

void ndHandleSsdp(NdEngine &e, const char *pkt, size_t len, const uint8_t from[4]) {
    if (pkt == nullptr || len < 12) return;
    if (!ndStartsWithCI(pkt, "HTTP/1.1 200") && !ndStartsWithCI(pkt, "HTTP/1.0 200")) return;

    char location[96];
    char st[64];
    char server[64];
    char usn[64];
    const bool hasLocation = ndSsdpHeader(pkt, len, "LOCATION", location, sizeof(location));
    ndSsdpHeader(pkt, len, "ST", st, sizeof(st));
    ndSsdpHeader(pkt, len, "SERVER", server, sizeof(server));
    const bool hasUsn = ndSsdpHeader(pkt, len, "USN", usn, sizeof(usn));
    if (!hasLocation && st[0] == '\0' && server[0] == '\0') return;

    uint8_t ip[4];
    const uint16_t port = ndParseLocation(location, ip);
    const uint8_t *ipPtr = ip;
    if (!ndIpSet(ip)) ipPtr = from; // hostname in LOCATION: the responder address is the host

    char type[32];
    ndShortSsdpType(st, type, sizeof(type));

    char name[64];
    if (server[0] != '\0') {
        ndCopyCStr(name, sizeof(name), server);
    } else if (hasUsn && usn[0] != '\0') {
        const char *uuid = ndStartsWithCI(usn, "uuid:") ? usn + 5 : usn;
        const char *cut = strstr(uuid, "::");
        ndCopyStr(name, sizeof(name), uuid, cut != nullptr ? (size_t)(cut - uuid) : strlen(uuid));
    } else {
        ndCopyCStr(name, sizeof(name), type);
    }

    ndUpsertSsdp(e, name, type, ipPtr, hasLocation ? port : 0, location);
}

/*
 * Drive mDNS service-type, mDNS browse, and SSDP phases sequentially with one UDP
 * socket at a time. Receives are capped to the engine buffer and oversized datagrams
 * are drained; polling yields every 10 ms, reports progress at 200 ms intervals,
 * honors Esc in UI mode, and stops sockets on every phase exit.
 */

bool ndSendMdnsQuery(WiFiUDP &udp, const char *name) {
    uint8_t buf[512];
    const uint16_t qclass = (uint16_t)(dns_wire::CLASS_IN | dns_wire::CLASS_QU); // QU bit: unicast reply
    const size_t n = dns_wire::buildQuery(buf, sizeof(buf), 0, name, dns_wire::TYPE_PTR, qclass);
    if (n == 0) return false;
    udp.beginPacket(IPAddress(224, 0, 0, 251), ND_MDNS_PORT);
    udp.write(buf, n);
    return udp.endPacket() == 1;
}

void ndSendSsdpSearch(WiFiUDP &udp) {
    udp.beginPacket(IPAddress(239, 255, 255, 250), ND_SSDP_PORT);
    udp.write(reinterpret_cast<const uint8_t *>(kMSearch), sizeof(kMSearch) - 1);
    udp.endPacket();
}

// Polls one socket until endMs. Returns false when the user cancelled (pollKeys only).
bool ndListen(
    NdEngine &e, WiFiUDP &udp, uint32_t endMs, uint8_t phase, uint32_t startMs, uint32_t totalMs,
    bool pollKeys, bool ssdp, const NdTick &tick
) {
    uint32_t lastTick = 0;
    for (;;) {
        const uint32_t now = millis();
        if ((int32_t)(now - endMs) >= 0) return true;
        if (pollKeys && check(EscPress)) return false;

        const int size = udp.parsePacket();
        if (size > 0) {
            const int n = udp.read(e.pkt, sizeof(e.pkt) - 1);
            if (n > 0) {
                e.pkt[n] = '\0';
                const IPAddress from = udp.remoteIP();
                const uint8_t fromIp[4] = {from[0], from[1], from[2], from[3]};
                if (ssdp) ndHandleSsdp(e, reinterpret_cast<const char *>(e.pkt), (size_t)n, fromIp);
                else ndHandleMdns(e, e.pkt, (size_t)n, fromIp);
            }
            udp.clear(); // release the remainder of an oversized datagram
        }

        if (tick.fn != nullptr && (uint32_t)(now - lastTick) >= 200) {
            const DeviceCounts counts = ndCounts(e);
            tick.fn(phase, counts.mdns, counts.ssdp, e.typeCount, now - startMs, totalMs, tick.ctx);
            lastTick = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Collects the service types to browse. Discovered types go first (capped to half the budget so a
// hostile meta-query answer can never crowd out the defaults), then the fallback list, all
// case-insensitively deduped.
size_t ndQueryTypes(const NdEngine &e, const char **out, size_t cap) {
    size_t n = 0;
    const size_t discoveredCap = cap / 2;
    for (size_t i = 0; i < e.typeCount && n < discoveredCap; ++i) {
        bool dup = false;
        for (size_t k = 0; k < n; ++k) {
            if (ndEqualsCI(out[k], e.serviceTypes[i])) {
                dup = true;
                break;
            }
        }
        if (!dup) out[n++] = e.serviceTypes[i];
    }
    for (size_t i = 0; i < ND_FALLBACK_TYPE_COUNT && n < cap; ++i) {
        bool dup = false;
        for (size_t k = 0; k < n; ++k) {
            if (ndEqualsCI(out[k], kFallbackServiceTypes[i])) {
                dup = true;
                break;
            }
        }
        if (!dup) out[n++] = kFallbackServiceTypes[i];
    }
    return n;
}

NdOutcome
ndScan(NdEngine &e, uint16_t windowMs, NdScanCallback cb, void *cbCtx, const NdTick &tick, bool pollKeys) {
    memset(&e, 0, sizeof(e));
    uint32_t total = windowMs;
    if (total < 2000) total = 2000;
    if (total > 60000) total = 60000;

    const uint32_t start = millis();
    const uint32_t typeEnd = start + total * 20 / 100;
    const uint32_t browseEnd = start + total * 50 / 100;
    const uint32_t ssdpEnd = start + total;

    WiFiUDP udp;
    bool ok = true;

    // Phase A1: ask which service types exist on this LAN.
    if (!udp.beginMulticast(IPAddress(224, 0, 0, 251), ND_MDNS_PORT)) {
        NZLOGE("NDISC", "mDNS socket bind failed");
        return NdOutcome::NoSocket;
    }
    ndSendMdnsQuery(udp, "_services._dns-sd._udp.local");
    ok = ndListen(e, udp, typeEnd, ND_PHASE_TYPES, start, total, pollKeys, false, tick);
    NZLOGI("NDISC", "mdns types=%u", (unsigned)e.typeCount);

    // Phase A2: browse every type; responders usually volunteer SRV/TXT/A with the PTR answer.
    if (ok) {
        const char *queries[ND_MAX_SERVICE_TYPES];
        const size_t qn = ndQueryTypes(e, queries, ND_MAX_SERVICE_TYPES);
        for (size_t i = 0; i < qn; ++i) {
            if (pollKeys && check(EscPress)) {
                ok = false;
                break;
            }
            ndSendMdnsQuery(udp, queries[i]);
            vTaskDelay(pdMS_TO_TICKS(40)); // pace the burst so small AP buffers are not overrun
        }
        if (ok) ok = ndListen(e, udp, browseEnd, ND_PHASE_BROWSE, start, total, pollKeys, false, tick);
    }
    udp.stop();
    NZLOGI("NDISC", "mdns devices=%u", (unsigned)ndCounts(e).mdns);

    // Phase B: SSDP/UPnP M-SEARCH over the same object, one socket at a time.
    if (ok) {
        if (!udp.beginMulticast(IPAddress(239, 255, 255, 250), ND_SSDP_PORT)) {
            NZLOGE("NDISC", "SSDP socket bind failed");
            udp.stop();
            return NdOutcome::NoSocket;
        }
        ndSendSsdpSearch(udp);
        ok = ndListen(e, udp, ssdpEnd, ND_PHASE_SSDP, start, total, pollKeys, true, tick);
        udp.stop();
    }
    udp.stop();

    if (!ok) {
        NZLOGI("NDISC", "scan cancelled devices=%u", (unsigned)e.count);
        return NdOutcome::Cancelled;
    }

    NZLOGI("NDISC", "scan done devices=%u", (unsigned)e.count);
    if (cb != nullptr) {
        for (size_t i = 0; i < e.count; ++i) {
            const NdDevice &d = e.devices[i];
            cb(d.type, d.name, d.ip, d.port, cbCtx);
        }
    }
    return NdOutcome::Completed;
}

/*
 * Progress and result rendering uses screen-derived row bounds, scroll clamping,
 * and dirty-flag redraws. The interactive list can append discovered endpoints
 * to /netdiscovery.txt without changing the engine's fixed storage.
 */

struct NdUi {
    NdEngine *engine;
    int scroll;
};

constexpr int ND_ROW_H = 2 * LH + 3; // name/address line plus type/info line

int ndListTop() { return BORDER_PAD_Y + FM * LH + 6 + LH + 6; }
int ndListBottom() { return uiContentBottom(true); }

int ndVisibleRows() {
    const int height = ndListBottom() - ndListTop();
    const int rows = height / ND_ROW_H;
    return rows < 1 ? 1 : rows;
}

int ndMaxScroll(const NdEngine &e) {
    const int maxScroll = (int)e.count - ndVisibleRows();
    return maxScroll > 0 ? maxScroll : 0;
}

const char *ndPhaseLabel(uint8_t phase) {
    if (phase == ND_PHASE_TYPES) return "Discovering service types";
    if (phase == ND_PHASE_BROWSE) return "Browsing mDNS services";
    return "Searching SSDP devices";
}

void ndUiTick(
    uint8_t phase, size_t mdnsCount, size_t ssdpCount, size_t typeCount, uint32_t elapsedMs, uint32_t totalMs,
    void *ctx
) {
    (void)typeCount; // the phase label already tells the user which pass is running
    (void)ctx;
    const uint16_t bg = bruceConfig.bgColor;
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;
    const int yPhase = BORDER_PAD_Y + FM * LH + 10;

    uiDrawText16(bruce_i18n::tr(ndPhaseLabel(phase)), tftWidth / 2, yPhase, w, uiTextBrightColor(), bg, 1);

    char counts[48];
    snprintf(counts, sizeof(counts), "mDNS %u   SSDP %u", (unsigned)mdnsCount, (unsigned)ssdpCount);
    uiDrawText16(counts, tftWidth / 2, yPhase + LH + 8, w, uiTextMutedColor(), bg, 1);

    const int barY = yPhase + 2 * LH + 18;
    const int barH = 8;
    tft.drawRect(left, barY, w, barH, uiBorderDimColor());
    int fill = 0;
    if (totalMs != 0) fill = (int)(((uint64_t)(w - 2) * elapsedMs) / totalMs);
    if (fill < 0) fill = 0;
    if (fill > w - 2) fill = w - 2;
    if (fill > 0) tft.fillRect(left + 1, barY + 1, fill, barH - 2, bruceConfig.priColor);

    uiDrawText16(bruce_i18n::tr("ESC: Cancel"), tftWidth / 2, barY + barH + 14, w, uiTextMutedColor(), bg, 1);
}

void ndDrawResults(NdUi &ui, bool full) {
    const NdEngine &e = *ui.engine;
    const uint16_t bg = bruceConfig.bgColor;
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;
    const int yHead = BORDER_PAD_Y + FM * LH + 6;
    const int listTop = ndListTop();
    const int listBottom = ndListBottom();
    const int rows = ndVisibleRows();
    const int addrW = 21 * LW; // widest "255.255.255.255:65535" fits, so addresses are never clipped

    if (full) {
        drawMainBorderWithTitle("Network Discovery");
        printCenterFootnote("UP/DN: Scroll  NEXT: Save  ESC: Exit");
    }

    // Counters and the visible-range marker change while scrolling, so this line is always redrawn.
    const DeviceCounts counts = ndCounts(e);
    char head[48];
    snprintf(head, sizeof(head), "mDNS %u   SSDP %u", (unsigned)counts.mdns, (unsigned)counts.ssdp);
    char range[24];
    if (e.count == 0) {
        snprintf(range, sizeof(range), "0/0");
    } else {
        size_t last = (size_t)(ui.scroll + rows);
        if (last > e.count) last = e.count;
        snprintf(
            range, sizeof(range), "%u-%u/%u", (unsigned)(ui.scroll + 1), (unsigned)last, (unsigned)e.count
        );
    }
    tft.fillRect(left, yHead, w, LH + 2, bg);
    uiDrawText16(head, left, yHead, w - 60, uiTextMutedColor(), bg, 0);
    uiDrawText16(range, left + w, yHead, 56, uiTextMutedColor(), bg, 2);

    tft.fillRect(left, listTop, w, listBottom - listTop, bg);
    if (e.count == 0) {
        uiDrawText16(
            bruce_i18n::tr("No devices found"),
            tftWidth / 2,
            listTop + ND_ROW_H / 2,
            w,
            uiTextBrightColor(),
            bg,
            1
        );
        return;
    }

    int y = listTop;
    for (int r = 0; r < rows && (size_t)(ui.scroll + r) < e.count; ++r, y += ND_ROW_H) {
        const NdDevice &d = e.devices[ui.scroll + r];
        char addr[ND_ADDR_CHARS];
        if (d.port != 0) {
            snprintf(
                addr,
                sizeof(addr),
                "%u.%u.%u.%u:%u",
                (unsigned)d.ip[0],
                (unsigned)d.ip[1],
                (unsigned)d.ip[2],
                (unsigned)d.ip[3],
                (unsigned)d.port
            );
        } else {
            snprintf(
                addr,
                sizeof(addr),
                "%u.%u.%u.%u",
                (unsigned)d.ip[0],
                (unsigned)d.ip[1],
                (unsigned)d.ip[2],
                (unsigned)d.ip[3]
            );
        }
        const char *title =
            d.name[0] != '\0' ? d.name : (d.type[0] != '\0' ? d.type : bruce_i18n::tr("Unknown"));
        uiDrawText16(title, left, y, w - addrW - 6, uiTextBrightColor(), bg, 0);
        uiDrawText16(
            addr, left + w, y, addrW, d.fromMdns ? bruceConfig.priColor : bruceConfig.secColor, bg, 2
        );

        char sub[64];
        if (d.type[0] != '\0') {
            snprintf(sub, sizeof(sub), "%s  %s", d.fromMdns ? "mDNS" : "SSDP", d.type);
        } else {
            snprintf(sub, sizeof(sub), "%s", d.fromMdns ? "mDNS" : "SSDP");
        }
        const int subMax = d.info[0] != '\0' ? (w * 55 / 100) : w;
        uiDrawText16(sub, left, y + LH + 3, subMax, uiTextMutedColor(), bg, 0);
        if (d.info[0] != '\0') {
            uiDrawText16(d.info, left + w, y + LH + 3, w - subMax - 4, uiTextMutedColor(), bg, 2);
        }
    }
}

void ndSaveLog(NdUi &ui) {
    const NdEngine &e = *ui.engine;
    if (e.count == 0) {
        displayInfo(bruce_i18n::tr("Nothing to save"), true);
        return;
    }
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) {
        displayError(bruce_i18n::tr("No storage found"), true);
        return;
    }
    File f = fs->open("/netdiscovery.txt", FILE_APPEND);
    if (!f) {
        displayError(bruce_i18n::tr("Save failed"), true);
        return;
    }
    f.print(bruce_i18n::tr("# Network Discovery log"));
    f.print("\n");
    for (size_t i = 0; i < e.count; ++i) {
        const NdDevice &d = e.devices[i];
        f.printf(
            "%s|%s|%s|%u.%u.%u.%u|%u\n",
            d.fromMdns ? "mdns" : "ssdp",
            d.type,
            d.name,
            (unsigned)d.ip[0],
            (unsigned)d.ip[1],
            (unsigned)d.ip[2],
            (unsigned)d.ip[3],
            (unsigned)d.port
        );
    }
    f.close();
    displaySuccess(String(bruce_i18n::tr("Saved to")) + " /netdiscovery.txt", true);
    NZLOGI("NDISC", "log saved devices=%u", (unsigned)e.count);
}

// Shared pre-flight: contiguous DMA for the radio stack, then a live STA link, then the WebUI.
// The WebUI owns ESPmDNS (port 5353), so it has to go before we bind our own multicast socket.
NdOutcome ndPreflight() {
    if (!radioHasMemForWifi(false)) return NdOutcome::LowRam;
    if (WiFi.status() != WL_CONNECTED) return NdOutcome::NotConnected;
    if (isWebUIActive) {
        cleanlyStopWebUiForWiFiFeature();
        vTaskDelay(pdMS_TO_TICKS(150));
        if (WiFi.status() != WL_CONNECTED) return NdOutcome::NotConnected;
    }
    return NdOutcome::Completed;
}

} // namespace

/*
 * Public UI and headless entries share DMA/STA preflight and per-scan allocation.
 * Preflight stops an active WebUI because ESPmDNS already owns port 5353; the STA
 * link is rechecked afterward. Both entries free the engine, while only the headless
 * path disables TFT ticks/key polling and returns completion to its caller.
 */

void netdiscovery_run() {
    if (!radioHasMemForWifi(false)) {
        displayError(radioLowRamError(bruce_i18n::tr("Low RAM")), true);
        return;
    }
    if (ndPreflight() != NdOutcome::Completed) {
        displayError(bruce_i18n::tr("Connect to WiFi first"), true);
        return;
    }

    NdEngine *engine = ndEngineAlloc();
    if (engine == nullptr) {
        displayError(bruce_i18n::tr("Out of memory"), true);
        return;
    }

    NdUi ui;
    ui.engine = engine;
    ui.scroll = 0;
    wakeUpScreen();
    drawMainBorderWithTitle(bruce_i18n::tr("Network Discovery"));

    NdTick tick;
    tick.fn = ndUiTick;
    tick.ctx = &ui;
    const NdOutcome outcome = ndScan(*engine, ND_SCAN_WINDOW_MS, nullptr, nullptr, tick, true);

    if (outcome == NdOutcome::NoSocket) {
        free(engine);
        displayError(bruce_i18n::tr("Socket error"), true);
        return;
    }
    if (outcome == NdOutcome::Cancelled && engine->count == 0) {
        free(engine);
        return;
    }

    const int maxScroll = ndMaxScroll(*engine);
    bool fullRedraw = true;
    bool listDirty = true;
    for (;;) {
        if (check(EscPress)) break;
        if (check(UpPress) || check(PrevPress)) {
            if (ui.scroll > 0) {
                --ui.scroll;
                listDirty = true;
            }
        } else if (check(DownPress)) {
            if (ui.scroll < maxScroll) {
                ++ui.scroll;
                listDirty = true;
            }
        } else if (check(NextPress) || check(SelPress)) {
            ndSaveLog(ui);
            fullRedraw = true; // the notice box overwrote the list
        }

        if (fullRedraw) {
            ndDrawResults(ui, true);
            fullRedraw = false;
            listDirty = false;
        } else if (listDirty) {
            ndDrawResults(ui, false);
            listDirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    free(engine);
    NZLOGI("NDISC", "browser closed");
}

bool netdiscovery_scan(uint16_t windowMs, NdScanCallback cb, void *ctx, NdOutcome *outcome) {
    const NdOutcome pre = ndPreflight();
    if (pre != NdOutcome::Completed) {
        if (outcome != nullptr) *outcome = pre;
        return false;
    }

    NdEngine *engine = ndEngineAlloc();
    if (engine == nullptr) {
        if (outcome != nullptr) *outcome = NdOutcome::NoMemory;
        return false;
    }

    NdTick noTick; // headless: no TFT, no key polling
    noTick.fn = nullptr;
    noTick.ctx = nullptr;

    const NdOutcome result = ndScan(*engine, windowMs, cb, ctx, noTick, false);
    free(engine);
    if (outcome != nullptr) *outcome = result;
    return result == NdOutcome::Completed;
}

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT
