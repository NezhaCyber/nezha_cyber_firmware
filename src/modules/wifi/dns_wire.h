#ifndef __DNS_WIRE_H__
#define __DNS_WIRE_H__

/*
 * DNS wire-format codec shared by the DNS lookup tool and the mDNS browser.
 *
 * Pure byte handling: no WiFi, no heap and no global state, so both consumers use the same parser
 * and the codec can be exercised on the host. Offsets are always into the whole message; the RDATA
 * helpers that may contain a compressed name take (pkt, pktLen, rdataOff, rdlen) because SRV, MX
 * and SOA targets normally point back into an earlier section of the packet.
 */

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include <stddef.h>
#include <stdint.h>

namespace dns_wire {

// RFC 1035 section 2.3.4 limits, plus the jump budget that breaks compression-pointer loops.
static constexpr size_t MAX_WIRE_NAME = 255;
static constexpr size_t MAX_LABEL_LEN = 63;
static constexpr uint8_t MAX_POINTER_JUMPS = 32;

// Recommended scratch size for the dotted/escaped text form. Names whose bytes need many \DDD
// escapes may not fit; parseName() then fails instead of overflowing the buffer.
static constexpr size_t MAX_NAME_TEXT = 256;

// EDNS0 OPT pseudo-RR payload size advertised by buildQuery() (RFC 6891).
static constexpr uint16_t EDNS_UDP_PAYLOAD = 1232;

// Header flag bits (RFC 1035 section 4.1.1).
static constexpr uint16_t FLAG_QR = 0x8000;
static constexpr uint16_t FLAG_OPCODE_MASK = 0x7800;
static constexpr uint16_t FLAG_AA = 0x0400;
static constexpr uint16_t FLAG_TC = 0x0200;
static constexpr uint16_t FLAG_RD = 0x0100;
static constexpr uint16_t FLAG_RA = 0x0080;
static constexpr uint16_t FLAG_RCODE_MASK = 0x000F;

// Record types touched by the DNS lookup tool and the mDNS browser.
static constexpr uint16_t TYPE_A = 1;
static constexpr uint16_t TYPE_NS = 2;
static constexpr uint16_t TYPE_CNAME = 5;
static constexpr uint16_t TYPE_SOA = 6;
static constexpr uint16_t TYPE_PTR = 12;
static constexpr uint16_t TYPE_MX = 15;
static constexpr uint16_t TYPE_TXT = 16;
static constexpr uint16_t TYPE_AAAA = 28;
static constexpr uint16_t TYPE_SRV = 33;
static constexpr uint16_t TYPE_OPT = 41;
static constexpr uint16_t TYPE_ANY = 255;

// CLASS_QU is the top bit of the question class (RFC 6762 section 5.4: ask for a unicast reply);
// CLASS_MASK strips it, and also the cache-flush bit answers use in the same position.
static constexpr uint16_t CLASS_IN = 1;
static constexpr uint16_t CLASS_ANY = 255;
static constexpr uint16_t CLASS_MASK = 0x7FFF;
static constexpr uint16_t CLASS_QU = 0x8000;

/*
 * Builds one UDP DNS query: 12-byte header (QR=0, RD=1, QDCOUNT=1), one question, and optionally
 * an EDNS0 OPT RR (type 41, class = EDNS_UDP_PAYLOAD, empty RDATA) with ARCOUNT=1.
 *
 * qclass is written verbatim, so the mDNS browser passes CLASS_IN | CLASS_QU with id 0, while the
 * DNS lookup tool passes CLASS_IN and may enable EDNS0.
 *
 * Returns the wire length, or 0 when the buffer is too small or the name is invalid (empty, empty
 * label, label longer than 63 bytes, more than 255 wire octets). A single trailing dot is accepted;
 * "." builds a root query.
 */
size_t buildQuery(
    uint8_t *buf, size_t cap, uint16_t id, const char *name, uint16_t qtype, uint16_t qclass = CLASS_IN,
    bool edns = false
);

/*
 * Decodes the name at *off, following compression pointers. A pointer must target an offset
 * strictly before the pointer itself, and at most MAX_POINTER_JUMPS links are followed, which
 * rejects loops and forward references.
 *
 * The output is the dotted ASCII form ("www.example.com", "." for the root). Bytes outside the
 * printable range 0x21..0x7E, and '\', are written as \DDD (three decimal digits) so the display
 * layer never sees raw binary. On success *off advances past the name in the original stream; on
 * failure out is set to "" and *off is unchanged.
 */
bool parseName(const uint8_t *pkt, size_t pktLen, size_t *off, char *out, size_t outCap);

/*
 * Receives one resource record. rdata points inside pkt; the RDATA offset needed by
 * parseSrv()/parseMx()/parseSoa() is (size_t)(rdata - pkt). name is the NUL-terminated owner name
 * in dotted form, pointing at a scratch buffer that is reused for the next record, so copy it when
 * it must outlive the callback.
 */
typedef void (*RecordCb)(
    uint16_t rtype, uint16_t rclass, uint32_t ttl, const uint8_t *rdata, uint16_t rdlen, const char *name,
    void *ctx
);

/*
 * Walks count resource records from *off. Call once per section (answer, authority, additional);
 * questions must be skipped first by the caller (parseName() followed by 4 bytes). Owner names are
 * decoded into a bounded scratch buffer, RDLEN is checked against the packet, and the walk stops at
 * the first malformed record. cb may be null to skip the records.
 *
 * Returns the number of complete records parsed; *off advances past the last one, or points at the
 * start of the first malformed record when the walk stopped early.
 */
size_t parseRecords(const uint8_t *pkt, size_t pktLen, size_t *off, uint16_t count, RecordCb cb, void *ctx);

// Header counters and flags, big-endian; 0 when the packet is shorter than the 12-byte header.
uint16_t questionCount(const uint8_t *pkt, size_t pktLen);
uint16_t answerCount(const uint8_t *pkt, size_t pktLen);
uint16_t authorityCount(const uint8_t *pkt, size_t pktLen);
uint16_t additionalCount(const uint8_t *pkt, size_t pktLen);
uint16_t flags(const uint8_t *pkt, size_t pktLen);

/*
 * Joins the TXT character-strings with single spaces. The result is truncated cleanly to outCap and
 * still returns true; false means the RDATA is malformed (a length byte overruns rdlen).
 */
bool parseTxt(const uint8_t *rdata, uint16_t rdlen, char *out, size_t outCap);

/*
 * Decodes SRV RDATA (priority, weight, port, target) at rdataOff. The target name is resolved
 * against the whole packet but never past the end of the RDATA, so a truncated record cannot run
 * into the next one. prio/weight/port may be null.
 */
bool parseSrv(
    const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, uint16_t *prio, uint16_t *weight,
    uint16_t *port, char *target, size_t targetCap
);

/*
 * Decodes MX RDATA (preference, exchange) at rdataOff. exchange is resolved with the same
 * whole-packet / RDATA-bound rule as parseSrv().
 */
bool parseMx(
    const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, uint16_t *pref, char *exchange,
    size_t exchangeCap
);

struct SoaInfo {
    char mname[128];
    char rname[128];
    uint32_t serial;
    uint32_t refresh;
    uint32_t retry;
    uint32_t expire;
    uint32_t minimum;
};

/*
 * Decodes SOA RDATA (mname, rname, serial, refresh, retry, expire, minimum) at rdataOff. Names
 * that do not fit the fixed fields make the call fail rather than truncate.
 */
bool parseSoa(const uint8_t *pkt, size_t pktLen, size_t rdataOff, uint16_t rdlen, SoaInfo *out);

} // namespace dns_wire

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT

#endif // __DNS_WIRE_H__
