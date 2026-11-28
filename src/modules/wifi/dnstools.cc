#include "dnstools.h"

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
} // namespace bruce_i18n
#endif
#include "core/logging/nzlog.h"
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ctype.h>
#include <esp_random.h>
#include <globals.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

/*
 * Fixed DNS/query, reply, text, report, and label capacities bound all network and
 * rendering work. UDP uses two 800 ms attempts, TCP fallback uses a 2 s read window,
 * and brute defaults and storage paths are shared by UI and CLI entry points.
 */
constexpr size_t DNS_QUERY_BUF = 512;
constexpr size_t DNS_REPLY_BUF = 4096;
constexpr size_t DNS_NAME_CAP = 253; // RFC 1035 text limit for a dotted name
constexpr uint16_t DNS_PORT = 53;
constexpr uint32_t DNS_UDP_TIMEOUT_MS = 800;
constexpr uint32_t DNS_TCP_TIMEOUT_MS = 2000;
constexpr uint8_t DNS_UDP_ATTEMPTS = 2; // first try + 1 retry

constexpr size_t TEXT_BUF_CAP = 1024;
constexpr size_t BRUTE_LOG_CAP = 2560;
constexpr size_t BRUTE_REPORT_CAP = 2944;
constexpr size_t SUB_LABEL_CAP = 96;
constexpr size_t BRUTE_NAME_CAP = 288;
constexpr uint16_t BRUTE_DEFAULT_MAX = 1000;
constexpr uint32_t BRUTE_REDRAW_MS = 150;
constexpr int TEXT_ROW_H = 18;

constexpr const char *WORDLIST_PATH = "/wordlists/subdomains.txt";
constexpr const char *BRUTE_SAVE_PATH = "/dns_brute.txt";

/*
 * TextBuf appends formatted output within the caller's capacity, saturating at the
 * final usable byte and always preserving NUL termination. Inactive/null outputs
 * still allow resolution when only LookupInfo is required.
 */
struct TextBuf {
    char *buf;
    size_t cap;
    size_t used;

    TextBuf(char *b = nullptr, size_t c = 0) : buf(b), cap(c), used(0) {
        if (buf != nullptr && cap > 0) buf[0] = '\0';
    }

    bool active() const { return buf != nullptr && cap > 1; }

    void addf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    void trimTrailingNewlines() {
        if (!active()) return;
        while (used > 0 && buf[used - 1] == '\n') buf[--used] = '\0';
    }
};

void TextBuf::addf(const char *fmt, ...) {
    if (!active() || used + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf + used, cap - used, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= cap - used) used = cap - 1;
    else used += (size_t)n;
    buf[used] = '\0';
}

/*
 * Validation helpers provide case-insensitive ASCII comparison, strict dotted-quad
 * parsing with exactly four 0-255 octets, bounded DNS-label/domain checks, and
 * printable sanitization for untrusted reply text.
 */
bool ieq(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// Strict dotted-quad parser: exactly four decimal octets, each 0..255, no trailing garbage.
bool parseIpv4(const char *s, uint8_t out[4]) {
    if (s == nullptr || out == nullptr) return false;
    const size_t len = strlen(s);
    if (len < 7 || len > 15) return false;

    int parts[4] = {0, 0, 0, 0};
    int index = 0;
    int value = 0;
    int digits = 0;
    for (size_t i = 0; i <= len; ++i) {
        const char c = (i < len) ? s[i] : '.';
        if (c == '.') {
            if (digits == 0 || value > 255) return false;
            if (index >= 4) return false;
            parts[index++] = value;
            value = 0;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + (c - '0');
        } else {
            return false;
        }
    }
    if (index != 4) return false;
    for (int i = 0; i < 4; ++i) out[i] = (uint8_t)parts[i];
    return true;
}

// Rejects empty labels, labels over 63 bytes, leading/trailing dots and characters that cannot be
// part of a host name. Underscores are kept because service records use them.
bool validDomain(const char *name) {
    if (name == nullptr) return false;
    const size_t len = strlen(name);
    if (len == 0 || len > DNS_NAME_CAP) return false;

    size_t label = 0;
    for (size_t i = 0; i <= len; ++i) {
        const char c = (i < len) ? name[i] : '.';
        if (c == '.') {
            if (label == 0 || label > dns_wire::MAX_LABEL_LEN) return false;
            if (name[i - 1] == '-') return false;
            label = 0;
        } else {
            const bool ok = isalnum((unsigned char)c) || c == '-' || c == '_';
            if (!ok) return false;
            if (label == 0 && c == '-') return false;
            ++label;
        }
    }
    return true;
}

bool endsWithDomain(const char *sub, const char *domain) {
    const size_t sl = strlen(sub);
    const size_t dl = strlen(domain);
    if (sl <= dl) return false;
    if (sub[sl - dl - 1] != '.') return false;
    return ieq(sub + sl - dl, domain);
}

void sanitizeAscii(char *s) {
    for (; *s != '\0'; ++s) {
        const unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) *s = '.';
    }
}

/*
 * The fixed qtype table is the single source for UI choices and case-insensitive
 * CLI conversion. Shared rcode labels and explanations keep lookup output consistent.
 */
struct QTypeEntry {
    const char *label;
    uint16_t type;
};

const QTypeEntry kQTypes[] = {
    {"A",     dns_wire::TYPE_A    },
    {"AAAA",  dns_wire::TYPE_AAAA },
    {"MX",    dns_wire::TYPE_MX   },
    {"TXT",   dns_wire::TYPE_TXT  },
    {"NS",    dns_wire::TYPE_NS   },
    {"PTR",   dns_wire::TYPE_PTR  },
    {"SOA",   dns_wire::TYPE_SOA  },
    {"CNAME", dns_wire::TYPE_CNAME},
};
constexpr size_t kQTypeCount = sizeof(kQTypes) / sizeof(kQTypes[0]);

const char *qtypeName(uint16_t type) {
    for (size_t i = 0; i < kQTypeCount; ++i) {
        if (kQTypes[i].type == type) return kQTypes[i].label;
    }
    if (type == dns_wire::TYPE_SRV) return "SRV";
    return nullptr;
}

const char *rcodeName(uint16_t rcode) {
    switch (rcode) {
        case 0: return "NOERROR";
        case 1: return "FORMERR";
        case 2: return "SERVFAIL";
        case 3: return "NXDOMAIN";
        case 4: return "NOTIMP";
        case 5: return "REFUSED";
        default: return nullptr;
    }
}

const char *rcodeExplain(uint16_t rcode) {
    switch (rcode) {
        case 1: return bruce_i18n::tr("Malformed query");
        case 2: return bruce_i18n::tr("Server failure");
        case 3: return bruce_i18n::tr("Domain does not exist");
        case 4: return bruce_i18n::tr("Not implemented by resolver");
        case 5: return bruce_i18n::tr("Query refused");
        default: return nullptr;
    }
}

/*
 * Static resolver, reply, text, brute-log, and progress storage is reused by UI
 * and CLI operations and assumes one active operation at a time. A custom resolver
 * overrides WiFi.dnsIP(0); otherwise the system value or 8.8.8.8 fallback is used.
 */
IPAddress s_customResolver;
bool s_hasCustomResolver = false;

uint8_t s_reply[DNS_REPLY_BUF];
char s_textBuf[TEXT_BUF_CAP];

struct BruteLog {
    char buf[BRUTE_LOG_CAP];
    size_t used;
    uint16_t dropped;
};

BruteLog s_bruteLog;

struct BruteProgress {
    char target[DNS_NAME_CAP + 1];
    char current[BRUTE_NAME_CAP];
    uint16_t total;
    uint16_t done;
    uint16_t found;
    uint32_t lastDraw;
    bool dirty;
};

BruteProgress s_bruteProg;
char s_bruteReport[BRUTE_REPORT_CAP];
char s_lastDomain[DNS_NAME_CAP + 1];

bool ipv4IsZero(const IPAddress &ip) { return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0; }

IPAddress activeResolver() {
    if (s_hasCustomResolver) return s_customResolver;
    IPAddress systemDns = WiFi.dnsIP(0);
    if (!ipv4IsZero(systemDns)) return systemDns;
    return IPAddress(8, 8, 8, 8);
}

/*
 * DNS transport binds an ephemeral UDP port, never port 53. UDP accepts only the
 * matching transaction ID before its bounded per-attempt deadline; TCP fallback
 * uses two-byte length framing and rejects replies outside the destination capacity.
 * Guards close UDP sockets and wordlist files on every scope exit.
 */
struct DnsTransport {
    WiFiUDP udp;
    bool open = false;

    bool begin() {
        open = udp.begin(0) != 0;
        return open;
    }

    void end() {
        if (open) udp.stop();
        open = false;
    }
};

struct TransportGuard {
    DnsTransport &transport;
    ~TransportGuard() { transport.end(); }
};

struct FileGuard {
    File &file;
    ~FileGuard() {
        if (file) file.close();
    }
};

bool udpExchange(
    DnsTransport &transport, const IPAddress &resolver, const uint8_t *query, size_t queryLen, uint16_t id,
    uint8_t *reply, size_t replyCap, size_t &replyLen
) {
    if (!transport.open) return false;

    for (uint8_t attempt = 0; attempt < DNS_UDP_ATTEMPTS; ++attempt) {
        if (!transport.udp.beginPacket(resolver, DNS_PORT)) return false;
        if (transport.udp.write(query, queryLen) != queryLen) return false;
        if (!transport.udp.endPacket()) return false;

        const uint32_t deadline = millis() + DNS_UDP_TIMEOUT_MS;
        while ((int32_t)(millis() - deadline) < 0) {
            const int available = transport.udp.parsePacket();
            if (available > 0) {
                const int want = (available < (int)replyCap) ? available : (int)replyCap;
                const int got = transport.udp.read(reply, want);
                if (available > want) transport.udp.clear();
                if (got >= 12) {
                    const uint16_t replyId = ((uint16_t)reply[0] << 8) | reply[1];
                    if (replyId == id) {
                        replyLen = (size_t)got;
                        return true;
                    }
                }
                continue; // stale datagram, keep waiting for our ID
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    return false;
}

bool readExact(WiFiClient &client, uint8_t *dst, size_t len, uint32_t deadline) {
    size_t got = 0;
    while (got < len) {
        if ((int32_t)(millis() - deadline) >= 0) return false;
        const int available = client.available();
        if (available > 0) {
            const size_t want = ((size_t)available < len - got) ? (size_t)available : len - got;
            const int readNow = client.read(dst + got, want);
            if (readNow > 0) {
                got += (size_t)readNow;
                continue;
            }
        }
        if (!client.connected()) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

bool tcpExchange(
    const IPAddress &resolver, const uint8_t *query, size_t queryLen, uint8_t *reply, size_t replyCap,
    size_t &replyLen
) {
    WiFiClient client;
    if (!client.connect(resolver, DNS_PORT, 1500)) return false;

    const uint8_t prefix[2] = {(uint8_t)(queryLen >> 8), (uint8_t)(queryLen & 0xFF)};
    bool ok = client.write(prefix, sizeof(prefix)) == sizeof(prefix);
    if (ok) ok = client.write(query, queryLen) == queryLen;

    if (ok) {
        const uint32_t deadline = millis() + DNS_TCP_TIMEOUT_MS;
        uint8_t lengthBytes[2] = {0, 0};
        ok = readExact(client, lengthBytes, sizeof(lengthBytes), deadline);
        if (ok) {
            const size_t wireLen = ((size_t)lengthBytes[0] << 8) | lengthBytes[1];
            if (wireLen < 12 || wireLen > replyCap) {
                ok = false;
            } else {
                ok = readExact(client, reply, wireLen, deadline);
                if (ok) replyLen = wireLen;
            }
        }
    }
    client.stop();
    return ok;
}

/*
 * Render bounded dns_wire records into TextBuf with TTLs and type-specific fields.
 * The render context also records the first valid A address for brute-force and
 * other callers that request LookupInfo, even when textual output is disabled.
 */
struct LookupInfo {
    uint16_t rcode = 0;
    bool hasA = false;
    uint8_t firstA[4] = {0, 0, 0, 0};
};

struct RenderCtx {
    TextBuf *text = nullptr;
    const uint8_t *pkt = nullptr;
    size_t pktLen = 0;
    uint16_t rendered = 0;
    bool hasA = false;
    uint8_t firstA[4] = {0, 0, 0, 0};
};

void renderRecord(
    uint16_t rtype, uint16_t rclass, uint32_t ttl, const uint8_t *rdata, uint16_t rdlen, const char *name,
    void *ctxPtr
) {
    RenderCtx *ctx = static_cast<RenderCtx *>(ctxPtr);
    if (ctx == nullptr) return;
    (void)rclass;
    (void)name;

    const size_t rdataOff = (size_t)(rdata - ctx->pkt);
    if (rdataOff > ctx->pktLen || (size_t)rdlen > ctx->pktLen - rdataOff) return;
    ctx->rendered++;
    TextBuf &text = *ctx->text;

    switch (rtype) {
        case dns_wire::TYPE_A:
            if (rdlen == 4) {
                if (!ctx->hasA) {
                    memcpy(ctx->firstA, rdata, 4);
                    ctx->hasA = true;
                }
                if (text.active()) {
                    text.addf(
                        "A  %u.%u.%u.%u  ttl %lu\n",
                        (unsigned)rdata[0],
                        (unsigned)rdata[1],
                        (unsigned)rdata[2],
                        (unsigned)rdata[3],
                        (unsigned long)ttl
                    );
                }
            }
            break;

        case dns_wire::TYPE_AAAA:
            if (rdlen == 16 && text.active()) {
                const IPAddress v6(IPv6, rdata);
                text.addf("AAAA  %s  ttl %lu\n", v6.toString().c_str(), (unsigned long)ttl);
            }
            break;

        case dns_wire::TYPE_NS:
        case dns_wire::TYPE_CNAME:
        case dns_wire::TYPE_PTR: {
            char target[dns_wire::MAX_NAME_TEXT];
            size_t off = rdataOff;
            if (!dns_wire::parseName(ctx->pkt, rdataOff + rdlen, &off, target, sizeof(target))) break;
            if (text.active()) {
                text.addf("%s  %s  ttl %lu\n", qtypeName(rtype), target, (unsigned long)ttl);
            }
            break;
        }

        case dns_wire::TYPE_MX: {
            uint16_t preference = 0;
            char exchange[dns_wire::MAX_NAME_TEXT];
            if (!dns_wire::parseMx(
                    ctx->pkt, ctx->pktLen, rdataOff, rdlen, &preference, exchange, sizeof(exchange)
                ))
                break;
            if (text.active()) {
                text.addf("MX  %u %s  ttl %lu\n", (unsigned)preference, exchange, (unsigned long)ttl);
            }
            break;
        }

        case dns_wire::TYPE_TXT: {
            char txt[dns_wire::MAX_NAME_TEXT];
            if (!dns_wire::parseTxt(rdata, rdlen, txt, sizeof(txt))) break;
            sanitizeAscii(txt);
            if (text.active()) text.addf("TXT  \"%s\"  ttl %lu\n", txt, (unsigned long)ttl);
            break;
        }

        case dns_wire::TYPE_SOA: {
            dns_wire::SoaInfo soa;
            if (!dns_wire::parseSoa(ctx->pkt, ctx->pktLen, rdataOff, rdlen, &soa)) break;
            if (text.active()) {
                text.addf("SOA  %s %s\n", soa.mname, soa.rname);
                text.addf(
                    "serial %lu refresh %lu retry %lu expire %lu min %lu  ttl %lu\n",
                    (unsigned long)soa.serial,
                    (unsigned long)soa.refresh,
                    (unsigned long)soa.retry,
                    (unsigned long)soa.expire,
                    (unsigned long)soa.minimum,
                    (unsigned long)ttl
                );
            }
            break;
        }

        default: {
            if (!text.active()) break;
            const char *label = qtypeName(rtype);
            if (label != nullptr)
                text.addf("%s  %u bytes  ttl %lu\n", label, (unsigned)rdlen, (unsigned long)ttl);
            else
                text.addf(
                    "TYPE%u  %u bytes  ttl %lu\n", (unsigned)rtype, (unsigned)rdlen, (unsigned long)ttl
                );
            break;
        }
    }
}

/*
 * Resolve one name/qtype using a random transaction ID, 512-byte query, and shared
 * 4096-byte reply buffer. Validate response ID/QR, retry over TCP when TC is set,
 * parse bounded records, and return status separately from rendered text/LookupInfo.
 */
bool dnsResolve(
    DnsTransport &transport, const char *name, uint16_t qtype, char *out, size_t outCap, LookupInfo &info
) {
    info = LookupInfo();
    TextBuf text(out, outCap);

    if (!WiFi.isConnected()) {
        if (text.active()) text.addf("%s", bruce_i18n::tr("WiFi not connected"));
        return false;
    }
    if (!transport.open && !transport.begin()) {
        if (text.active()) text.addf("%s", bruce_i18n::tr("Cannot open UDP socket"));
        return false;
    }

    const uint16_t id = (uint16_t)(esp_random() & 0xFFFFu);
    uint8_t query[DNS_QUERY_BUF];
    const size_t queryLen = dns_wire::buildQuery(query, sizeof(query), id, name, qtype);
    if (queryLen == 0) {
        if (text.active()) text.addf("%s", bruce_i18n::tr("Invalid domain name"));
        return false;
    }

    const IPAddress resolver = activeResolver();
    size_t replyLen = 0;
    if (!udpExchange(transport, resolver, query, queryLen, id, s_reply, sizeof(s_reply), replyLen)) {
        if (text.active()) text.addf("%s", bruce_i18n::tr("Query timed out"));
        NZLOGW("DNS", "timeout %s type %u", name, (unsigned)qtype);
        return false;
    }

    uint16_t flags = dns_wire::flags(s_reply, replyLen);
    uint16_t replyId = ((uint16_t)s_reply[0] << 8) | s_reply[1];
    if (replyId != id || (flags & dns_wire::FLAG_QR) == 0) {
        if (text.active()) text.addf("%s", bruce_i18n::tr("Malformed DNS reply"));
        return false;
    }

    bool tcpFailed = false;
    if ((flags & dns_wire::FLAG_TC) != 0) {
        size_t tcpLen = 0;
        if (tcpExchange(resolver, query, queryLen, s_reply, sizeof(s_reply), tcpLen)) {
            replyLen = tcpLen;
            replyId = ((uint16_t)s_reply[0] << 8) | s_reply[1];
            if (replyId != id || (dns_wire::flags(s_reply, replyLen) & dns_wire::FLAG_QR) == 0) {
                if (text.active()) text.addf("%s", bruce_i18n::tr("Malformed DNS reply"));
                return false;
            }
            flags = dns_wire::flags(s_reply, replyLen);
        } else {
            tcpFailed = true;
        }
    }

    const uint16_t questionCount = dns_wire::questionCount(s_reply, replyLen);
    const uint16_t answerCount = dns_wire::answerCount(s_reply, replyLen);
    const uint16_t rcode = flags & dns_wire::FLAG_RCODE_MASK;
    info.rcode = rcode;

    size_t off = 12;
    char scratch[dns_wire::MAX_NAME_TEXT];
    for (uint16_t i = 0; i < questionCount; ++i) {
        if (!dns_wire::parseName(s_reply, replyLen, &off, scratch, sizeof(scratch))) {
            if (text.active()) text.addf("%s", bruce_i18n::tr("Malformed DNS reply"));
            return false;
        }
        if (off + 4 > replyLen) {
            if (text.active()) text.addf("%s", bruce_i18n::tr("Malformed DNS reply"));
            return false;
        }
        off += 4;
    }

    if (text.active()) {
        const char *typeLabel = qtypeName(qtype);
        if (typeLabel != nullptr) {
            text.addf("%s %s @%s\n", name, typeLabel, resolver.toString().c_str());
        } else {
            text.addf("%s TYPE%u @%s\n", name, (unsigned)qtype, resolver.toString().c_str());
        }
        const char *rcName = rcodeName(rcode);
        if (rcName != nullptr)
            text.addf("rcode %s  %s %u\n", rcName, bruce_i18n::tr("answers"), (unsigned)answerCount);
        else text.addf("rcode %u\n", (unsigned)rcode);
        if (tcpFailed) text.addf("%s\n", bruce_i18n::tr("Truncated reply (TCP retry failed)"));

        const char *explain = rcodeExplain(rcode);
        if (rcode != 0) {
            if (explain != nullptr) text.addf("%s\n", explain);
            else text.addf("%s %u\n", bruce_i18n::tr("DNS error"), (unsigned)rcode);
        }
    }

    RenderCtx render;
    render.text = &text;
    render.pkt = s_reply;
    render.pktLen = replyLen;
    dns_wire::parseRecords(s_reply, replyLen, &off, answerCount, renderRecord, &render);
    info.hasA = render.hasA;
    if (render.hasA) memcpy(info.firstA, render.firstA, 4);

    if (text.active()) {
        if (rcode == 0 && answerCount == 0) text.addf("%s\n", bruce_i18n::tr("No records"));
        if (rcode == 0 && answerCount > 0 && render.rendered == 0) {
            text.addf("%s\n", bruce_i18n::tr("Malformed DNS reply"));
        }
        text.trimTrailingNewlines();
    }

    NZLOGD(
        "DNS", "%s type %u rcode %u answers %u", name, (unsigned)qtype, (unsigned)rcode, (unsigned)answerCount
    );
    return true;
}

/*
 * Subdomain enumeration checks radio DMA before opening transport, loads the
 * wordlist before Wi-Fi state changes, and rejects matching random-label wildcard
 * answers. Bounded lines feed a capped query loop with fixed logs, dirty progress,
 * 10 ms yields, Esc abort, and RAII file/socket cleanup.
 */
void randomLabel(char out[13]) {
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 12; ++i) out[i] = kChars[esp_random() % (sizeof(kChars) - 1)];
    out[12] = '\0';
}

enum class ProbeResult : uint8_t { Found, NoData, NetworkError };

ProbeResult probeA(DnsTransport &transport, const char *name, uint8_t ip[4]) {
    LookupInfo info;
    if (!dnsResolve(transport, name, dns_wire::TYPE_A, nullptr, 0, info)) return ProbeResult::NetworkError;
    if (info.rcode != 0 || !info.hasA) return ProbeResult::NoData;
    memcpy(ip, info.firstA, 4);
    return ProbeResult::Found;
}

// One line from the wordlist, capped at cap-1 bytes. Overlong lines are consumed up to '\n' so the
// reader never resumes in the middle of a word.
bool readWordLine(File &file, char *out, size_t cap) {
    size_t used = 0;
    bool any = false;
    while (file.available()) {
        const int c = file.read();
        if (c < 0) break;
        any = true;
        if (c == '\n') break;
        if (used + 1 < cap) out[used++] = (char)c;
    }
    if (!any) {
        out[0] = '\0';
        return false;
    }
    out[used] = '\0';
    return true;
}

const char *trimWord(char *word) {
    while (*word == ' ' || *word == '\t' || *word == '\r') ++word;
    size_t len = strlen(word);
    while (len > 0 && (word[len - 1] == ' ' || word[len - 1] == '\t' || word[len - 1] == '\r')) {
        word[--len] = '\0';
    }
    return word;
}

void bruteLogCb(const char *sub, const uint8_t ip[4], void *ctx) {
    BruteLog *log = static_cast<BruteLog *>(ctx);
    if (log == nullptr) return;
    char line[128];
    snprintf(
        line,
        sizeof(line),
        "%s -> %u.%u.%u.%u",
        sub,
        (unsigned)ip[0],
        (unsigned)ip[1],
        (unsigned)ip[2],
        (unsigned)ip[3]
    );
    const size_t len = strlen(line);
    if (log->used + len + 2 > sizeof(log->buf)) {
        log->dropped++;
        return;
    }
    if (log->used > 0) log->buf[log->used++] = '\n';
    memcpy(log->buf + log->used, line, len);
    log->used += len;
    log->buf[log->used] = '\0';
}

void drawBruteProgress(const BruteProgress &progress) {
    const int left = BORDER_PAD_X + 4;
    const int width = tftWidth - 2 * BORDER_PAD_X - 8;
    const int top = BORDER_PAD_Y + FM * LH + 6;
    const uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(left, top, width, 84, bg);

    const String title = String(bruce_i18n::tr("Target")) + ": " + progress.target;
    uiDrawText16(title, left, top, width, uiTextBrightColor(), bg);

    const int barY = top + 20;
    tft.drawRect(left, barY, width, 8, uiBorderDimColor());
    if (progress.total > 0) {
        const int fill = (int)((uint32_t)(width - 2) * progress.done / progress.total);
        if (fill > 0) tft.fillRect(left + 1, barY + 1, fill, 6, bruceConfig.priColor);
    }

    char line[96];
    snprintf(
        line,
        sizeof(line),
        "%s %u/%u   %s %u",
        bruce_i18n::tr("Scanned"),
        (unsigned)progress.done,
        (unsigned)progress.total,
        bruce_i18n::tr("Found"),
        (unsigned)progress.found
    );
    uiDrawText16(line, left, top + 34, width, uiTextBrightColor(), bg);
    uiDrawText16(String(progress.current), left, top + 52, width, uiTextMutedColor(), bg);
}

void drawBruteFrame() {
    drawMainBorderWithTitle("Subdomain Brute");
    printCenterFootnote("ESC=Abort");
}

enum class BruteOutcome : uint8_t { Done, Aborted, Cancelled, NoStorage, NoWordlist, NoWifi, Wildcard };

BruteOutcome bruteRun(
    const char *listPath, uint16_t maxQueries, void (*cb)(const char *sub, const uint8_t ip[4], void *ctx),
    void *ctx
) {
    // Phase 1 — pre-flight guard: never touch the WiFi driver without DMA headroom.
    if (!radioHasMemForWifi(false)) {
        displayError(radioLowRamError(bruce_i18n::tr("Low RAM")), true);
        return BruteOutcome::NoWifi;
    }
    if (maxQueries == 0) maxQueries = BRUTE_DEFAULT_MAX;

    // Phase 2 — prompt for the base domain (session default = last used).
    String domain = keyboard(String(s_lastDomain), DNS_NAME_CAP, "Base domain");
    domain.trim();
    if (domain.isEmpty() || domain == "\x1B") return BruteOutcome::Cancelled;
    if (!validDomain(domain.c_str())) {
        displayError("Invalid domain name", true);
        return BruteOutcome::Cancelled;
    }

    // Phase 3 — load the wordlist BEFORE any WiFi state change so a failed radio bring-up can never
    // strand a half-read file or unmount the card underneath it.
    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        displayError("No storage found", true);
        return BruteOutcome::NoStorage;
    }
    File wordlist = fs->open(listPath, FILE_READ);
    if (!wordlist) {
        displayError(String(bruce_i18n::tr("Wordlist not found")) + ": " + listPath, true);
        return BruteOutcome::NoWordlist;
    }
    FileGuard wordlistGuard{wordlist};

    // Phase 4 — STA must be up. radioHasMemForWifi(false) above already guaranteed the DMA block,
    // so this connect cannot unmount the SD card holding the open wordlist.
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA) || !WiFi.isConnected()) {
            displayError("WiFi not connected", true);
            return BruteOutcome::NoWifi;
        }
    }

    strncpy(s_lastDomain, domain.c_str(), sizeof(s_lastDomain) - 1);
    s_lastDomain[sizeof(s_lastDomain) - 1] = '\0';

    DnsTransport transport;
    if (!transport.begin()) {
        displayError("Cannot open UDP socket", true);
        return BruteOutcome::NoWifi;
    }
    TransportGuard transportGuard{transport};

    // Wildcard detection: two random 12-character labels resolving to the same address means every
    // name in the zone answers, so brute results would be meaningless.
    char label1[13];
    char label2[13];
    randomLabel(label1);
    randomLabel(label2);
    char probeName[BRUTE_NAME_CAP];
    uint8_t ip1[4] = {0, 0, 0, 0};
    uint8_t ip2[4] = {0, 0, 0, 0};

    snprintf(probeName, sizeof(probeName), "%s.%s", label1, s_lastDomain);
    const ProbeResult probe1 = probeA(transport, probeName, ip1);
    snprintf(probeName, sizeof(probeName), "%s.%s", label2, s_lastDomain);
    const ProbeResult probe2 = probeA(transport, probeName, ip2);

    // Wildcard detection: a random 12-character label resolving to ANY address means the zone
    // answers for every name (round-robin/CDN wildcards return different addresses per query,
    // so comparing the two answers is not enough - any hit is the wildcard signature).
    if (probe1 == ProbeResult::Found || probe2 == ProbeResult::Found) {
        displayError("Wildcard DNS detected", true);
        NZLOGW("DNS", "wildcard zone %s", s_lastDomain);
        return BruteOutcome::Wildcard;
    }
    if (probe1 == ProbeResult::NetworkError && probe2 == ProbeResult::NetworkError) {
        displayError("Query timed out", true);
        return BruteOutcome::NoWifi;
    }

    // Phase 5 — dirty-flag progress loop.
    memset(&s_bruteProg, 0, sizeof(s_bruteProg));
    strncpy(s_bruteProg.target, s_lastDomain, sizeof(s_bruteProg.target) - 1);
    s_bruteProg.total = maxQueries;
    s_bruteProg.lastDraw = 0;
    s_bruteProg.dirty = true;

    drawBruteFrame();
    drawBruteProgress(s_bruteProg);
    s_bruteProg.dirty = false;
    s_bruteProg.lastDraw = millis();

    char word[SUB_LABEL_CAP];
    char current[BRUTE_NAME_CAP];
    uint16_t sent = 0;
    bool aborted = false;

    while (sent < maxQueries) {
        wakeUpScreen();
        if (!readWordLine(wordlist, word, sizeof(word))) break;
        const char *sub = trimWord(word);
        if (*sub == '\0' || *sub == '#') continue;

        const size_t subLen = strlen(sub);
        const size_t domainLen = strlen(s_lastDomain);
        if (endsWithDomain(sub, s_lastDomain)) {
            if (subLen > DNS_NAME_CAP) continue;
            snprintf(current, sizeof(current), "%s", sub);
        } else {
            if (subLen + 1 + domainLen > DNS_NAME_CAP) continue;
            snprintf(current, sizeof(current), "%s.%s", sub, s_lastDomain);
        }
        if (!validDomain(current)) continue;

        strncpy(s_bruteProg.current, current, sizeof(s_bruteProg.current) - 1);
        s_bruteProg.dirty = true;

        if (check(EscPress) || returnToMenu) {
            aborted = true;
            break;
        }

        LookupInfo info;
        dnsResolve(transport, current, dns_wire::TYPE_A, nullptr, 0, info);
        sent++;
        s_bruteProg.done = sent;
        // Only NOERROR answers count as hits: an NXDOMAIN/SERVFAIL reply that still carries an
        // A record in the answer section must not mark the subdomain as found.
        if (info.rcode == 0 && info.hasA) {
            s_bruteProg.found++;
            if (cb != nullptr) cb(sub, info.firstA, ctx);
        }
        s_bruteProg.dirty = true;

        const uint32_t now = millis();
        if (now - s_bruteProg.lastDraw >= BRUTE_REDRAW_MS) {
            drawBruteProgress(s_bruteProg);
            s_bruteProg.dirty = false;
            s_bruteProg.lastDraw = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));

        if (check(EscPress) || returnToMenu) {
            aborted = true;
            break;
        }
    }

    if (s_bruteProg.dirty) {
        drawBruteProgress(s_bruteProg);
        s_bruteProg.dirty = false;
    }

    NZLOGI(
        "DNS",
        "brute %s done=%u found=%u aborted=%d",
        s_lastDomain,
        (unsigned)s_bruteProg.done,
        (unsigned)s_bruteProg.found,
        aborted ? 1 : 0
    );
    return aborted ? BruteOutcome::Aborted : BruteOutcome::Done;
}

/*
 * Display newline-delimited results with clamped scrolling and dirty-flag redraws
 * every input pass. When enabled, Select offers an append to /dns_brute.txt;
 * storage failures are reported without discarding the in-memory report.
 */
int textLineCount(const char *body) {
    if (body == nullptr || *body == '\0') return 0;
    int count = 1;
    for (const char *p = body; *p != '\0'; ++p) {
        if (*p == '\n') count++;
    }
    return count;
}

const char *textLineAt(const char *body, int index) {
    const char *p = body;
    while (index > 0) {
        const char *nl = strchr(p, '\n');
        if (nl == nullptr) return nullptr;
        p = nl + 1;
        --index;
    }
    return p;
}

bool saveBruteReport(const char *body) {
    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        displayError("No storage found", true);
        return false;
    }
    File file = fs->open(BRUTE_SAVE_PATH, FILE_APPEND);
    if (!file) {
        displayError("Cannot write file", true);
        return false;
    }
    file.print(body);
    file.print("\n");
    file.close();
    displaySuccess("Saved to /dns_brute.txt", true);
    return true;
}

void showTextScreen(const char *title, const char *body, bool allowSave) {
    if (body == nullptr) body = "";
    drawMainBorderWithTitle(title);

    const int left = BORDER_PAD_X + 4;
    const int width = tftWidth - 2 * BORDER_PAD_X - 8;
    const int top = uiContentTop();
    const int footerY = uiFooterTop();
    const int bottom = uiContentBottom(true);
    const int rows = ((bottom - top) / TEXT_ROW_H) > 0 ? ((bottom - top) / TEXT_ROW_H) : 1;
    const int totalLines = textLineCount(body);
    const int maxScroll = (totalLines > rows) ? (totalLines - rows) : 0;

    int scroll = 0;
    bool dirty = true;
    bool done = false;
    while (!done && !returnToMenu) {
        wakeUpScreen();

        if (dirty) {
            tft.fillRect(left, top, width, bottom - top, bruceConfig.bgColor);
            int y = top;
            for (int row = 0; row < rows; ++row) {
                const char *linePtr = textLineAt(body, scroll + row);
                if (linePtr == nullptr) break;
                char line[192];
                size_t used = 0;
                while (*linePtr != '\0' && *linePtr != '\n' && used + 1 < sizeof(line)) {
                    line[used++] = *linePtr++;
                }
                line[used] = '\0';
                uint16_t color = uiTextBrightColor();
                if (strncmp(line, "rcode ", 6) == 0) {
                    color = (strstr(line, "NOERROR") != nullptr) ? TFT_GREEN : TFT_RED;
                }
                uiDrawText16(String(line), left, y, width, color, bruceConfig.bgColor);
                y += TEXT_ROW_H;
            }

            const char *help = allowSave ? "UP/DN=Scroll  SEL=Save  ESC=Back" : "UP/DN=Scroll  ESC=Back";
            printCenterFootnote(help);
            if (maxScroll > 0) {
                char position[16];
                snprintf(position, sizeof(position), "%d/%d", scroll + 1, maxScroll + 1);
                tft.setTextSize(FP);
                tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
                tft.drawRightString(position, tftWidth - BORDER_OFFSET_FROM_SCREEN_EDGE, footerY, 1);
            }
            dirty = false;
        }

        if (check(EscPress)) {
            done = true;
        } else if (check(UpPress) || check(PrevPress)) {
            if (scroll > 0) {
                --scroll;
                dirty = true;
            }
        } else if (check(DownPress) || check(NextPress)) {
            if (scroll < maxScroll) {
                ++scroll;
                dirty = true;
            }
        } else if (allowSave && check(SelPress)) {
            int action = 0;
            std::vector<Option> saveOpts = {
                {"Save to /dns_brute.txt", [&action]() { action = 1; }},
                {"Back",                   []() {}                    },
            };
            loopOptions(saveOpts, MENU_TYPE_SUBMENU, "Save Results");
            if (action == 1) saveBruteReport(body);
            drawMainBorderWithTitle(title);
            dirty = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    check(SelPress);
    check(EscPress);
}

/*
 * Menu operations collect and validate lookup, reverse-PTR, brute, and resolver
 * inputs, then reuse the same core routines as the public CLI API. Local option
 * vectors keep nested menus independent of global menu storage.
 */
void dnsLookupUi() {
    String name = keyboard("", DNS_NAME_CAP, "Domain name");
    name.trim();
    if (name.isEmpty() || name == "\x1B") return;
    if (!validDomain(name.c_str())) {
        displayError("Invalid domain name", true);
        return;
    }

    int picked = -1;
    std::vector<Option> typeOptions;
    typeOptions.reserve(kQTypeCount);
    for (size_t i = 0; i < kQTypeCount; ++i) {
        const size_t index = i;
        typeOptions.push_back({kQTypes[i].label, [&picked, index]() { picked = (int)index; }});
    }
    loopOptions(typeOptions, MENU_TYPE_SUBMENU, "Record Type");
    if (picked < 0 || picked >= (int)kQTypeCount) return;

    dnstools_query(name.c_str(), kQTypes[picked].type, s_textBuf, sizeof(s_textBuf));
    showTextScreen("DNS Lookup", s_textBuf, false);
}

void dnsReverseUi() {
    String ip = keyboard("", 15, "IPv4 address");
    ip.trim();
    if (ip.isEmpty() || ip == "\x1B") return;

    char arpaName[64];
    if (!dnstools_reverse(ip.c_str(), arpaName, sizeof(arpaName))) {
        displayError("Invalid IPv4 address", true);
        return;
    }
    dnstools_query(arpaName, dns_wire::TYPE_PTR, s_textBuf, sizeof(s_textBuf));
    showTextScreen("Reverse PTR", s_textBuf, false);
}

void dnsBruteUi() {
    s_bruteLog.used = 0;
    s_bruteLog.dropped = 0;
    s_bruteLog.buf[0] = '\0';

    const BruteOutcome outcome = bruteRun(WORDLIST_PATH, BRUTE_DEFAULT_MAX, bruteLogCb, &s_bruteLog);
    if (outcome != BruteOutcome::Done && outcome != BruteOutcome::Aborted) return;

    TextBuf report(s_bruteReport, sizeof(s_bruteReport));
    report.addf("%s: %s\n", bruce_i18n::tr("Target"), s_bruteProg.target);
    report.addf(
        "%s %u/%u   %s %u",
        bruce_i18n::tr("Scanned"),
        (unsigned)s_bruteProg.done,
        (unsigned)s_bruteProg.total,
        bruce_i18n::tr("Found"),
        (unsigned)s_bruteProg.found
    );
    if (outcome == BruteOutcome::Aborted) report.addf("  [%s]", bruce_i18n::tr("Aborted"));
    if (s_bruteLog.dropped > 0) {
        report.addf("\n%s %u", bruce_i18n::tr("List truncated, dropped"), (unsigned)s_bruteLog.dropped);
    }
    if (s_bruteLog.used > 0) report.addf("\n%s", s_bruteLog.buf);
    else report.addf("\n%s", bruce_i18n::tr("No subdomains found"));

    showTextScreen("Subdomain Brute", s_bruteReport, true);
}

void dnsResolverUi() {
    const String title = String(bruce_i18n::tr("Resolver")) + ": " + activeResolver().toString();
    int action = 0;
    std::vector<Option> localOpts = {
        {"Use System DNS",  [&action]() { action = 1; }},
        {"Custom Resolver", [&action]() { action = 2; }},
        {"Back",            []() {}                    },
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, title.c_str());

    if (action == 1) {
        s_hasCustomResolver = false;
        displaySuccess("Using system DNS", true);
    } else if (action == 2) {
        String ip = keyboard(activeResolver().toString(), 15, "Custom resolver");
        ip.trim();
        if (ip.isEmpty() || ip == "\x1B") return;
        uint8_t octets[4] = {0, 0, 0, 0};
        if (!parseIpv4(ip.c_str(), octets)) {
            displayError("Invalid IPv4 address", true);
            return;
        }
        // Reject addresses that can never be a DNS resolver: unspecified, multicast and broadcast.
        // (parseIpv4 itself stays permissive because reverse PTR of 0.0.0.0 is legitimate input.)
        if ((octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0) || octets[0] >= 224 ||
            (octets[0] == 255 && octets[1] == 255 && octets[2] == 255 && octets[3] == 255)) {
            displayError("Invalid IPv4 address", true);
            return;
        }
        s_customResolver = IPAddress(octets[0], octets[1], octets[2], octets[3]);
        s_hasCustomResolver = true;
        displaySuccess("Custom resolver set", true);
    }
}

} // namespace

/*
 * Public entry points expose the interactive toolkit, bounded textual queries,
 * strict IPv4-to-in-addr.arpa conversion, qtype lookup, and callback-based brute
 * enumeration. dnstools_run checks Wi-Fi DMA before connecting; query transport
 * is explicitly closed before returning success or failure.
 */
void dnstools_run() {
    if (!radioHasMemForWifi(false)) {
        displayError(radioLowRamError(bruce_i18n::tr("Low RAM")), true);
        return;
    }
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA) || !WiFi.isConnected()) {
            displayError("WiFi not connected", true);
            return;
        }
    }

    std::vector<Option> localOpts = {
        {"Lookup",          dnsLookupUi  },
        {"Reverse PTR",     dnsReverseUi },
        {"Subdomain Brute", dnsBruteUi   },
        {"Resolver",        dnsResolverUi},
        {"Exit",            []() {}      },
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "DNS Toolkit");
}

bool dnstools_query(const char *name, uint16_t qtype, char *out, size_t outCap) {
    if (out != nullptr && outCap > 0) out[0] = '\0';
    if (name == nullptr || name[0] == '\0') {
        TextBuf text(out, outCap);
        if (text.active()) text.addf("%s", bruce_i18n::tr("Invalid domain name"));
        return false;
    }
    DnsTransport transport;
    LookupInfo info;
    const bool ok = dnsResolve(transport, name, qtype, out, outCap, info);
    transport.end();
    return ok;
}

bool dnstools_reverse(const char *ipv4, char *out, size_t outCap) {
    if (out == nullptr || outCap == 0) return false;
    out[0] = '\0';
    uint8_t octets[4] = {0, 0, 0, 0};
    if (!parseIpv4(ipv4, octets)) return false;
    snprintf(
        out,
        outCap,
        "%u.%u.%u.%u.in-addr.arpa",
        (unsigned)octets[3],
        (unsigned)octets[2],
        (unsigned)octets[1],
        (unsigned)octets[0]
    );
    return true;
}

uint16_t dnstools_type_from_name(const char *type) {
    if (type == nullptr) return 0;
    for (size_t i = 0; i < kQTypeCount; ++i) {
        if (ieq(kQTypes[i].label, type)) return kQTypes[i].type;
    }
    return 0;
}

bool dnstools_brute(
    const char *listPath, uint16_t maxQueries, void (*cb)(const char *sub, const uint8_t ip[4], void *ctx),
    void *ctx
) {
    const char *path = (listPath != nullptr && listPath[0] != '\0') ? listPath : WORDLIST_PATH;
    const BruteOutcome outcome = bruteRun(path, maxQueries, cb, ctx);
    return outcome == BruteOutcome::Done;
}

// Headless brute for the serial CLI/WebUI: same engine contract as bruteRun, but no TFT keyboard,
// no progress screen and no blocking dialogs - serial commands must return on their own. All
// failures come back as false (details in the NZLOG) instead of displayError(wait-for-key).
bool dnstools_brute_headless(
    const char *domain, const char *listPath, uint16_t maxQueries,
    void (*cb)(const char *sub, const uint8_t ip[4], void *ctx), void *ctx
) {
    if (!radioHasMemForWifi(false)) {
        NZLOGW("DNS", "brute refused: low DMA");
        return false;
    }
    if (domain == nullptr || !validDomain(domain)) {
        NZLOGW("DNS", "brute refused: invalid domain");
        return false;
    }
    if (maxQueries == 0) maxQueries = BRUTE_DEFAULT_MAX;
    const char *path = (listPath != nullptr && listPath[0] != '\0') ? listPath : WORDLIST_PATH;

    // Load the wordlist BEFORE any WiFi state change, exactly like the interactive path.
    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        NZLOGW("DNS", "brute refused: no storage");
        return false;
    }
    File wordlist = fs->open(path, FILE_READ);
    if (!wordlist) {
        NZLOGW("DNS", "brute refused: wordlist missing %s", path);
        return false;
    }
    FileGuard wordlistGuard{wordlist};

    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA) || !WiFi.isConnected()) return false;
    }

    DnsTransport transport;
    if (!transport.begin()) return false;
    TransportGuard transportGuard{transport};

    // Wildcard detection (same conservative rule as the interactive path): any random-label
    // answer means the zone resolves everything and the wordlist run would be meaningless.
    char label1[13];
    char label2[13];
    randomLabel(label1);
    randomLabel(label2);
    char probeName[BRUTE_NAME_CAP];
    uint8_t ip1[4] = {0, 0, 0, 0};
    uint8_t ip2[4] = {0, 0, 0, 0};

    snprintf(probeName, sizeof(probeName), "%s.%s", label1, domain);
    const ProbeResult probe1 = probeA(transport, probeName, ip1);
    snprintf(probeName, sizeof(probeName), "%s.%s", label2, domain);
    const ProbeResult probe2 = probeA(transport, probeName, ip2);

    if (probe1 == ProbeResult::Found || probe2 == ProbeResult::Found) {
        NZLOGW("DNS", "wildcard zone %s", domain);
        return false;
    }
    if (probe1 == ProbeResult::NetworkError && probe2 == ProbeResult::NetworkError) return false;

    char word[SUB_LABEL_CAP];
    char current[BRUTE_NAME_CAP];
    uint16_t sent = 0;

    while (sent < maxQueries) {
        if (!readWordLine(wordlist, word, sizeof(word))) break;
        const char *sub = trimWord(word);
        if (*sub == '\0' || *sub == '#') continue;

        const size_t subLen = strlen(sub);
        const size_t domainLen = strlen(domain);
        if (endsWithDomain(sub, domain)) {
            if (subLen > DNS_NAME_CAP) continue;
            snprintf(current, sizeof(current), "%s", sub);
        } else {
            if (subLen + 1 + domainLen > DNS_NAME_CAP) continue;
            snprintf(current, sizeof(current), "%s.%s", sub, domain);
        }
        if (!validDomain(current)) continue;

        LookupInfo info;
        dnsResolve(transport, current, dns_wire::TYPE_A, nullptr, 0, info);
        sent++;
        if (info.rcode == 0 && info.hasA) {
            if (cb != nullptr) cb(sub, info.firstA, ctx);
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // keep the serial task schedulable
    }

    NZLOGI("DNS", "brute %s done=%u", domain, (unsigned)sent);
    return true;
}

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT
