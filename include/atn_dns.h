/*
 * Athanor authoritative DNS (REQ-2.3). Spec: docs/DNS.md, DEC-0011, RFC 1035.
 *
 * Purpose:  Answer IN queries for the in-memory zone. Never recurse.
 * Policy:   No libresolv, no c-ares, no forwarding to 8.8.8.8 / 1.1.1.1.
 */
#ifndef ATN_DNS_H
#define ATN_DNS_H

#include "atn_crypto.h"

#define ATN_DNS_MAX_MSG      512u
#define ATN_DNS_MAX_NAME     255u
#define ATN_DNS_MAX_RR       64u
#define ATN_DNS_MAX_RDATA    256u
#define ATN_DNS_IDLE_MS      3000
#define ATN_DNS_CLI_PORT     1053u

#define ATN_DNS_TYPE_A       1u
#define ATN_DNS_TYPE_NS      2u
#define ATN_DNS_TYPE_SOA     6u
#define ATN_DNS_TYPE_TXT     16u
#define ATN_DNS_TYPE_AAAA    28u
#define ATN_DNS_CLASS_IN     1u

#define ATN_DNS_RCODE_NOERROR   0u
#define ATN_DNS_RCODE_FORMERR   1u
#define ATN_DNS_RCODE_NXDOMAIN  3u
#define ATN_DNS_RCODE_NOTIMP    4u
#define ATN_DNS_RCODE_REFUSED   5u

typedef struct {
    char     name[ATN_DNS_MAX_NAME + 1u]; /* FQDN, lowercase, no trailing dot */
    uint16_t type;
    uint16_t rrclass;
    uint32_t ttl;
    uint16_t rdlen;
    uint8_t  rdata[ATN_DNS_MAX_RDATA];
} atn_dns_rr;

typedef struct {
    atn_dns_rr rr[ATN_DNS_MAX_RR];
    unsigned   n;
    char       apex[ATN_DNS_MAX_NAME + 1u];
} atn_dns_zone;

typedef struct {
    uint16_t id;
    uint16_t qtype;
    uint16_t qclass;
    char     qname[ATN_DNS_MAX_NAME + 1u];
    int      rd;
} atn_dns_q;

typedef struct {
    intptr_t udp;
    intptr_t tcp;
    uint16_t port;
    uint16_t tcp_port; /* DEC-0024: may differ from port on Windows */
    atn_dns_zone zone;
    uint8_t  last_wire[ATN_DNS_MAX_MSG + 2u];
    size_t   last_wire_len;
    uint32_t last_peer;   /* IPv4 host order of last querier */
    uint16_t last_peer_port;
} atn_dns_srv;

void atn_dns_zone_init(atn_dns_zone *z);
int  atn_dns_upsert_a(atn_dns_zone *z, const char *name, uint32_t ipv4_host, uint32_t ttl);
int  atn_dns_delete(atn_dns_zone *z, const char *name, uint16_t type);

/*
 * Purpose:  Parse one DNS query message.
 * Spec:     RFC 1035 §§4.1.1–4.1.2.
 * Returns:  ATN_OK or ATN_ERR_PARAM (FORMERR material).
 */
int atn_dns_parse_query(const uint8_t *msg, size_t n, atn_dns_q *q);

/*
 * Purpose:  Build an authoritative response for q into out.
 * Spec:     docs/DNS.md RCODE table. Writes *outn bytes, <= 512.
 */
int atn_dns_respond(const atn_dns_zone *z, const atn_dns_q *q,
                    const uint8_t *query, size_t qn,
                    uint8_t *out, size_t *outn, size_t max);

int atn_dns_listen(atn_dns_srv *s, uint16_t port);
uint16_t atn_dns_port(const atn_dns_srv *s);
uint16_t atn_dns_tcp_port(const atn_dns_srv *s);
int atn_dns_serve_one(atn_dns_srv *s, int timeout_ms);
void atn_dns_close(atn_dns_srv *s);

int atn_dns_query_udp(uint16_t port, const char *qname, uint16_t qtype,
                      uint8_t *resp, size_t *n, size_t max, int timeout_ms);
int atn_dns_query_tcp(uint16_t port, const char *qname, uint16_t qtype,
                      uint8_t *resp, size_t *n, size_t max, int timeout_ms);

#endif
