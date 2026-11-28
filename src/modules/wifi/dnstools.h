#ifndef __DNSTOOLS_H__
#define __DNSTOOLS_H__

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include "modules/wifi/dns_wire.h"
#include <stddef.h>
#include <stdint.h>

/*
 * DNS Toolkit — raw DNS client (A/AAAA/MX/TXT/NS/PTR/SOA/CNAME), reverse PTR and subdomain brute.
 *
 * The resolver is session-local: dnstools_run() lets the user keep the system resolver or enter a
 * custom IPv4, and the CLI engine shares the same selection. Nothing is written to /bruce.conf.
 *
 * Lookups bind an ephemeral UDP port and never listen on port 53 locally (the Evil Portal DNSServer
 * owns that port). Replies carrying the TC bit are re-issued over TCP to the same resolver.
 */

// Menu UI: Lookup / Reverse PTR / Subdomain Brute / Resolver / Exit.
void dnstools_run();

/*
 * Resolves one name/type. When out/outCap allow it, out receives printable text: a query summary,
 * the rcode line, one line per answer record and a translated explanation on failure. Returns true
 * only when a well-formed reply was received (an NXDOMAIN reply still counts as true). Requires an
 * established STA connection; no WiFi state is changed here.
 */
bool dnstools_query(const char *name, uint16_t qtype, char *out, size_t outCap);

// Builds the in-addr.arpa name for an IPv4 string ("1.2.3.4" -> "4.3.2.1.in-addr.arpa").
bool dnstools_reverse(const char *ipv4, char *out, size_t outCap);

// Maps "a"/"AAAA"/"mx"/... to dns_wire::TYPE_*; returns 0 for an unknown type name.
uint16_t dnstools_type_from_name(const char *type);

/*
 * Subdomain brute force against the session resolver. Prompts for the base domain (the last one is
 * kept as the default), reads listPath line by line with a fixed 96-byte buffer and invokes cb for
 * every A record found. maxQueries is the hard cap; 0 selects the 1000-query default. Wildcard DNS
 * is detected with two random labels and refuses the run. The TFT shows a progress screen and Esc
 * aborts between queries. Returns false when the run was aborted or could not start.
 */
bool dnstools_brute(
    const char *listPath, uint16_t maxQueries, void (*cb)(const char *sub, const uint8_t ip[4], void *ctx),
    void *ctx
);

/*
 * Headless brute for the serial CLI/WebUI: domain is caller-supplied (no TFT keyboard), no
 * progress screen and no blocking dialogs. Same wildcard detection and rcode==0 hit rule as the
 * interactive path. Returns false when the run was refused or aborted, true when it completed.
 */
bool dnstools_brute_headless(
    const char *domain, const char *listPath, uint16_t maxQueries,
    void (*cb)(const char *sub, const uint8_t ip[4], void *ctx), void *ctx
);

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT

#endif // __DNSTOOLS_H__
