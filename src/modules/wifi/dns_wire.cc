#include "dns_wire.h"

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include <string.h>

namespace dns_wire {
namespace {

inline uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

inline uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// Decodes one (possibly compressed) name into dotted ASCII. *off is committed only once the whole
// name decoded successfully, so a malformed name leaves the caller's cursor untouched.
bool decodeName(const uint8_t *pkt, size_t pktLen, size_t *off, char *out, size_t outCap) {
    size_t pos = *off;
    size_t endOff = 0;
    size_t wireUsed = 0;
    size_t used = 0;
    size_t labels = 0;
    uint8_t jumps = 0;
    bool jumped = false;

    for (;;) {
        if (pos >= pktLen) return false;
        const uint8_t len = pkt[pos];
        if (len == 0) {
            if (!jumped) endOff = pos + 1;
            break;
        }
        if ((len & 0xC0) == 0xC0) { // compression pointer: two bytes, target strictly backwards
            if (pos + 2 > pktLen) return false;
            if (!jumped) {
                endOff = pos + 2;
                jumped = true;
            }
            const size_t target = (size_t)(((size_t)(len & 0x3F) << 8) | pkt[pos + 1]);
            if (target >= pos) return false;
            if (++jumps > MAX_POINTER_JUMPS) return false;
            pos = target;
            continue;
        }
        if ((len & 0xC0) != 0) return false; // 0x40/0x80 extended label types are not supported
        if (wireUsed + 1 + len > MAX_WIRE_NAME - 1) return false; // leave room for the root octet
        if (pos + 1 + len > pktLen) return false;
        wireUsed += 1 + len;
        if (labels++ > 0) {
            if (used + 1 >= outCap) return false;
            out[used++] = '.';
        }
        for (size_t i = 0; i < len; ++i) {
            const uint8_t c = pkt[pos + 1 + i];
            if (c < 0x21 || c > 0x7E || c == '\\') {
                if (used + 4 >= outCap) return false; // reserve the terminating NUL byte
                out[used++] = '\\';
                out[used++] = (char)('0' + (c / 100) % 10);
                out[used++] = (char)('0' + (c / 10) % 10);
                out[used++] = (char)('0' + c % 10);
            } else {
                if (used + 1 >= outCap) return false;
                out[used++] = (char)c;
            }
        }
        pos += 1 + len;
    }

    if (used == 0) { // root
        if (outCap < 2) return false;
        out[used++] = '.';
    }
    out[used] = '\0';
    *off = endOff;
    return true;
}

} // namespace

size_t buildQuery(
    uint8_t *buf, size_t cap, uint16_t id, const char *name, uint16_t qtype, uint16_t qclass, bool edns
) {
    if (buf == nullptr || name == nullptr || name[0] == '\0') return 0;

    size_t o = 12; // header is filled in last, once the rest is known to fit
    size_t wireName = 0;
    if (!(name[0] == '.' && name[1] == '\0')) {
        const char *p = name;
        while (*p != '\0') {
            const char *dot = strchr(p, '.');
            const size_t lab = dot != nullptr ? (size_t)(dot - p) : strlen(p);
            if (lab == 0 || lab > MAX_LABEL_LEN) return 0;
            if (wireName + 1 + lab > MAX_WIRE_NAME - 1) return 0;
            if (o + 1 + lab > cap) return 0;
            buf[o++] = (uint8_t)lab;
            memcpy(buf + o, p, lab);
            o += lab;
            wireName += 1 + lab;
            if (dot == nullptr) break;
            p = dot + 1;
        }
    }
    if (o + 1 > cap) return 0;
    buf[o++] = 0;

    if (o + 4 > cap) return 0;
    put16(buf + o, qtype);
    put16(buf + o + 2, qclass);
    o += 4;

    if (edns) {
        if (o + 11 > cap) return 0;
        uint8_t *opt = buf + o;
        opt[0] = 0;                       // root owner
        put16(opt + 1, TYPE_OPT);         // type
        put16(opt + 3, EDNS_UDP_PAYLOAD); // class = requestor UDP payload size
        memset(opt + 5, 0, 4);            // TTL: extended RCODE/version/flags all zero
        put16(opt + 9, 0);                // RDLEN
        o += 11;
    }

    put16(buf + 0, id);
    put16(buf + 2, FLAG_RD);
    put16(buf + 4, 1);
    put16(buf + 6, 0);
    put16(buf + 8, 0);
    put16(buf + 10, edns ? 1 : 0);
    return o;
}

bool parseName(const uint8_t *pkt, size_t pktLen, size_t *off, char *out, size_t outCap) {
    if (pkt == nullptr || off == nullptr || out == nullptr || outCap == 0) return false;
    out[0] = '\0';
    if (*off >= pktLen) return false;
    if (!decodeName(pkt, pktLen, off, out, outCap)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

size_t parseRecords(const uint8_t *pkt, size_t pktLen, size_t *off, uint16_t count, RecordCb cb, void *ctx) {
    if (pkt == nullptr || off == nullptr) return 0;

    char name[MAX_NAME_TEXT];
    size_t pos = *off;
    size_t parsed = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t recordStart = pos;
        if (!parseName(pkt, pktLen, &pos, name, sizeof(name))) break;
        if (pos + 10 > pktLen) {
            pos = recordStart;
            break;
        }
        const uint16_t rtype = get16(pkt + pos);
        const uint16_t rclass = get16(pkt + pos + 2);
        const uint32_t ttl = get32(pkt + pos + 4);
        const uint16_t rdlen = get16(pkt + pos + 8);
        if ((size_t)rdlen > pktLen - (pos + 10)) {
            pos = recordStart;
            break;
        }
        pos += 10;
        if (cb != nullptr) cb(rtype, rclass, ttl, pkt + pos, rdlen, name, ctx);
        pos += rdlen;
        parsed++;
    }
    *off = pos;
    return parsed;
}

uint16_t questionCount(const uint8_t *pkt, size_t pktLen) {
    return (pkt != nullptr && pktLen >= 12) ? get16(pkt + 4) : 0;
}

uint16_t answerCount(const uint8_t *pkt, size_t pktLen) {
    return (pkt != nullptr && pktLen >= 12) ? get16(pkt + 6) : 0;
}

uint16_t authorityCount(const uint8_t *pkt, size_t pktLen) {
    return (pkt != nullptr && pktLen >= 12) ? get16(pkt + 8) : 0;
}

uint16_t additionalCount(const uint8_t *pkt, size_t pktLen) {
    return (pkt != nullptr && pktLen >= 12) ? get16(pkt + 10) : 0;
}

uint16_t flags(const uint8_t *pkt, size_t pktLen) {
    return (pkt != nullptr && pktLen >= 12) ? get16(pkt + 2) : 0;
}

bool parseTxt(const uint8_t *rdata, uint16_t rdlen, char *out, size_t outCap) {
    if (out == nullptr || outCap == 0) return false;
    out[0] = '\0';
    if (rdata == nullptr) return rdlen == 0;

    size_t r = 0;
    size_t used = 0;
    bool first = true;
    while (r < rdlen) {
        const uint8_t len = rdata[r++];
        if ((size_t)len > rdlen - r) {
            out[0] = '\0';
            return false;
        }
        if (!first) {
            if (used + 1 >= outCap) break; // truncate cleanly
            out[used++] = ' ';
        }
        first = false;
        for (uint8_t i = 0; i < len; ++i) {
            if (used + 1 >= outCap) {
                out[used] = '\0';
                return true;
            }
            out[used++] = (char)rdata[r + i];
        }
        r += len;
    }
    out[used] = '\0';
    return true;
}

bool parseSrv(
    const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, uint16_t *prio, uint16_t *weight,
    uint16_t *port, char *target, size_t targetCap
) {
    if (target != nullptr && targetCap != 0) target[0] = '\0';
    if (pkt == nullptr || target == nullptr || targetCap == 0) return false;
    if (rdataOff > pktLen || rdlen < 7 || (size_t)rdlen > pktLen - rdataOff) return false;

    if (prio != nullptr) *prio = get16(pkt + rdataOff);
    if (weight != nullptr) *weight = get16(pkt + rdataOff + 2);
    if (port != nullptr) *port = get16(pkt + rdataOff + 4);

    size_t pos = rdataOff + 6;
    if (!parseName(pkt, rdataOff + rdlen, &pos, target, targetCap)) {
        target[0] = '\0';
        return false;
    }
    return true;
}

bool parseMx(
    const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, uint16_t *pref, char *exchange,
    size_t exchangeCap
) {
    if (exchange != nullptr && exchangeCap != 0) exchange[0] = '\0';
    if (pkt == nullptr || exchange == nullptr || exchangeCap == 0) return false;
    if (rdataOff > pktLen || rdlen < 3 || (size_t)rdlen > pktLen - rdataOff) return false;

    if (pref != nullptr) *pref = get16(pkt + rdataOff);

    size_t pos = rdataOff + 2;
    if (!parseName(pkt, rdataOff + rdlen, &pos, exchange, exchangeCap)) {
        exchange[0] = '\0';
        return false;
    }
    return true;
}

bool parseSoa(const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, SoaInfo *out) {
    if (out == nullptr) return false;
    memset(out, 0, sizeof(*out));
    if (pkt == nullptr || rdataOff > pktLen || (size_t)rdlen > pktLen - rdataOff) return false;

    const size_t end = rdataOff + rdlen;
    size_t pos = rdataOff;
    if (!parseName(pkt, end, &pos, out->mname, sizeof(out->mname))) return false;
    if (!parseName(pkt, end, &pos, out->rname, sizeof(out->rname))) return false;
    if (pos + 20 > end) return false;

    out->serial = get32(pkt + pos);
    out->refresh = get32(pkt + pos + 4);
    out->retry = get32(pkt + pos + 8);
    out->expire = get32(pkt + pos + 12);
    out->minimum = get32(pkt + pos + 16);
    return true;
}

} // namespace dns_wire

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT
