/*
 * Module: atn_dns.c
 * REQ:    REQ-2.3
 * Spec:   docs/DNS.md, DEC-0011, RFC 1035 §§3–4
 */

#include "atn_dns.h"
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET atn_sock;
#  define ATN_INV INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <unistd.h>
   typedef int atn_sock;
#  define ATN_INV (-1)
#endif

static atn_sock sock_cast(intptr_t v)
{
    return (atn_sock)v;
}

static void sock_close(atn_sock s)
{
    if (s == ATN_INV) {
        return;
    }
#if defined(ATN_OS_WINDOWS)
    closesocket(s);
#else
    close(s);
#endif
}

static int tcp_read_n(atn_sock s, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int r;
#if defined(ATN_OS_WINDOWS)
        r = recv(s, (char *)buf + (int)got, (int)(n - got), 0);
#else
        r = (int)recv(s, buf + got, n - got, 0);
#endif
        if (r <= 0) {
            return ATN_ERR_SOCK;
        }
        got += (size_t)r;
    }
    return ATN_OK;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void lower_copy(char *dst, const char *src, size_t max)
{
    size_t i;
    for (i = 0; i + 1u < max && src[i] != 0; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        dst[i] = c;
    }
    dst[i] = 0;
}

static int in_zone(const char *apex, const char *name)
{
    size_t al, nl;
    if (apex == NULL || name == NULL) {
        return 0;
    }
    al = strlen(apex);
    nl = strlen(name);
    if (nl == al && memcmp(name, apex, al) == 0) {
        return 1;
    }
    if (nl > al + 1u && name[nl - al - 1u] == '.' &&
        memcmp(name + (nl - al), apex, al) == 0) {
        return 1;
    }
    return 0;
}

/* RFC 1035 §4.1.4: decode a domain name, following at most 10 pointers. */
static int decode_name(const uint8_t *msg, size_t n, size_t *off,
                       char *out, size_t max)
{
    size_t o = *off, hops = 0, outi = 0;
    int jumped = 0;
    size_t end = *off;
    if (o >= n) {
        return ATN_ERR_PARAM;
    }
    for (;;) {
        uint8_t lab;
        if (o >= n) {
            return ATN_ERR_PARAM;
        }
        lab = msg[o];
        if ((lab & 0xc0u) == 0xc0u) {
            if (o + 1u >= n) {
                return ATN_ERR_PARAM;
            }
            if (hops++ > 10u) {
                return ATN_ERR_PARAM;
            }
            if (!jumped) {
                end = o + 2u;
                jumped = 1;
            }
            o = (size_t)(((lab & 0x3fu) << 8) | msg[o + 1u]);
            if (o >= n) {
                return ATN_ERR_PARAM;
            }
            continue;
        }
        if ((lab & 0xc0u) != 0) {
            return ATN_ERR_PARAM;
        }
        o++;
        if (lab == 0) {
            if (outi == 0) {
                out[0] = 0; /* root */
            } else {
                out[outi] = 0;
            }
            if (!jumped) {
                end = o;
            }
            *off = end;
            return ATN_OK;
        }
        if (lab > 63u || o + lab > n) {
            return ATN_ERR_PARAM;
        }
        if (outi > 0) {
            if (outi + 1u >= max) {
                return ATN_ERR_LEN;
            }
            out[outi++] = '.';
        }
        if (outi + lab >= max) {
            return ATN_ERR_LEN;
        }
        {
            uint8_t i;
            for (i = 0; i < lab; i++) {
                char c = (char)msg[o + i];
                if (c >= 'A' && c <= 'Z') {
                    c = (char)(c - 'A' + 'a');
                }
                out[outi++] = c;
            }
        }
        o += lab;
    }
}

static int encode_name(const char *name, uint8_t *out, size_t *used, size_t max)
{
    size_t i = 0, start = 0, pos = 0;
    if (name == NULL || out == NULL || used == NULL) {
        return ATN_ERR_PARAM;
    }
    if (name[0] == 0) {
        if (max < 1) {
            return ATN_ERR_LEN;
        }
        out[0] = 0;
        *used = 1;
        return ATN_OK;
    }
    while (name[i] != 0) {
        if (name[i] == '.') {
            size_t lab = i - start;
            if (lab == 0 || lab > 63u || pos + 1u + lab + 1u > max) {
                return ATN_ERR_PARAM;
            }
            out[pos++] = (uint8_t)lab;
            memcpy(out + pos, name + start, lab);
            pos += lab;
            start = i + 1u;
        }
        i++;
    }
    {
        size_t lab = i - start;
        if (lab == 0 || lab > 63u || pos + 1u + lab + 1u > max) {
            return ATN_ERR_PARAM;
        }
        out[pos++] = (uint8_t)lab;
        memcpy(out + pos, name + start, lab);
        pos += lab;
    }
    out[pos++] = 0;
    *used = pos;
    return ATN_OK;
}

static int add_rr(atn_dns_zone *z, const char *name, uint16_t type,
                  uint32_t ttl, const uint8_t *rdata, uint16_t rdlen)
{
    atn_dns_rr *rr;
    if (z == NULL || name == NULL || (rdlen > 0 && rdata == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (rdlen > ATN_DNS_MAX_RDATA || z->n >= ATN_DNS_MAX_RR) {
        return ATN_ERR_LEN;
    }
    rr = &z->rr[z->n];
    memset(rr, 0, sizeof(*rr));
    lower_copy(rr->name, name, sizeof(rr->name));
    rr->type = type;
    rr->rrclass = ATN_DNS_CLASS_IN;
    rr->ttl = ttl;
    rr->rdlen = rdlen;
    if (rdlen > 0) {
        memcpy(rr->rdata, rdata, rdlen);
    }
    z->n++;
    return ATN_OK;
}

static int encode_soa(uint8_t *out, size_t *n, size_t max)
{
    uint8_t *p = out;
    size_t used = 0, mlen = 0, rlen = 0;
    int rc;
    rc = encode_name("ns.atn.test", p, &mlen, max);
    if (rc != ATN_OK) {
        return rc;
    }
    p += mlen;
    rc = encode_name("hostmaster.atn.test", p, &rlen, max - mlen);
    if (rc != ATN_OK) {
        return rc;
    }
    p += rlen;
    used = mlen + rlen;
    if (used + 20u > max) {
        return ATN_ERR_LEN;
    }
    wr32(p, 1);      /* SERIAL */
    wr32(p + 4, 3600);
    wr32(p + 8, 600);
    wr32(p + 12, 86400);
    wr32(p + 16, 60);
    *n = used + 20u;
    return ATN_OK;
}

void atn_dns_zone_init(atn_dns_zone *z)
{
    uint8_t a[4] = { 127, 0, 0, 1 };
    uint8_t ns[64], soa[256], txt[16];
    size_t nslen = 0, soalen = 0;
    if (z == NULL) {
        return;
    }
    memset(z, 0, sizeof(*z));
    lower_copy(z->apex, "atn.test", sizeof(z->apex));
    (void)encode_name("ns.atn.test", ns, &nslen, sizeof(ns));
    (void)encode_soa(soa, &soalen, sizeof(soa));
    txt[0] = 7;
    memcpy(txt + 1, "athanor", 7);
    (void)add_rr(z, "atn.test", ATN_DNS_TYPE_SOA, 60, soa, (uint16_t)soalen);
    (void)add_rr(z, "atn.test", ATN_DNS_TYPE_NS, 60, ns, (uint16_t)nslen);
    (void)add_rr(z, "atn.test", ATN_DNS_TYPE_TXT, 60, txt, 8);
    (void)add_rr(z, "ns.atn.test", ATN_DNS_TYPE_A, 60, a, 4);
    (void)add_rr(z, "node1.atn.test", ATN_DNS_TYPE_A, 60, a, 4);
}

int atn_dns_upsert_a(atn_dns_zone *z, const char *name, uint32_t ipv4_host, uint32_t ttl)
{
    unsigned i;
    uint8_t a[4];
    char nm[ATN_DNS_MAX_NAME + 1u];
    if (z == NULL || name == NULL) {
        return ATN_ERR_PARAM;
    }
    lower_copy(nm, name, sizeof(nm));
    a[0] = (uint8_t)(ipv4_host >> 24);
    a[1] = (uint8_t)(ipv4_host >> 16);
    a[2] = (uint8_t)(ipv4_host >> 8);
    a[3] = (uint8_t)ipv4_host;
    for (i = 0; i < z->n; i++) {
        if (z->rr[i].type == ATN_DNS_TYPE_A &&
            strcmp(z->rr[i].name, nm) == 0) {
            memcpy(z->rr[i].rdata, a, 4);
            z->rr[i].rdlen = 4;
            z->rr[i].ttl = ttl;
            return ATN_OK;
        }
    }
    return add_rr(z, nm, ATN_DNS_TYPE_A, ttl, a, 4);
}

int atn_dns_delete(atn_dns_zone *z, const char *name, uint16_t type)
{
    unsigned i;
    char nm[ATN_DNS_MAX_NAME + 1u];
    if (z == NULL || name == NULL) {
        return ATN_ERR_PARAM;
    }
    lower_copy(nm, name, sizeof(nm));
    for (i = 0; i < z->n; i++) {
        if (z->rr[i].type == type && strcmp(z->rr[i].name, nm) == 0) {
            if (i + 1u < z->n) {
                memmove(&z->rr[i], &z->rr[i + 1u],
                        sizeof(atn_dns_rr) * (z->n - i - 1u));
            }
            z->n--;
            atn_memzero(&z->rr[z->n], sizeof(atn_dns_rr));
            return ATN_OK;
        }
    }
    return ATN_ERR_STATE;
}

int atn_dns_parse_query(const uint8_t *msg, size_t n, atn_dns_q *q)
{
    size_t off;
    uint16_t flags, qd, an, ns, ar, qtype, qclass;
    int rc;
    if (msg == NULL || q == NULL || n < 12u) {
        return ATN_ERR_PARAM;
    }
    memset(q, 0, sizeof(*q));
    q->id = rd16(msg);
    flags = rd16(msg + 2);
    qd = rd16(msg + 4);
    an = rd16(msg + 6);
    ns = rd16(msg + 8);
    ar = rd16(msg + 10);
    (void)an;
    (void)ns;
    (void)ar;
    if ((flags >> 15) != 0) {
        return ATN_ERR_PARAM; /* QR=1 is a response */
    }
    q->rd = (flags >> 8) & 1;
    if (((flags >> 11) & 0xfu) != 0) {
        q->qtype = 0xffffu; /* sentinel: NOTIMP */
        return ATN_OK;
    }
    if (qd != 1u) {
        return ATN_ERR_PARAM;
    }
    off = 12;
    rc = decode_name(msg, n, &off, q->qname, sizeof(q->qname));
    if (rc != ATN_OK) {
        return rc;
    }
    if (off + 4u > n) {
        return ATN_ERR_PARAM;
    }
    qtype = rd16(msg + off);
    qclass = rd16(msg + off + 2);
    q->qtype = qtype;
    q->qclass = qclass;
    return ATN_OK;
}

static int write_rr(uint8_t *out, size_t *pos, size_t max, const atn_dns_rr *rr,
                    int compress_qname, const char *qname)
{
    size_t p = *pos, nlen = 0;
    int rc;
    if (compress_qname && strcmp(rr->name, qname) == 0 && p + 2u + 10u + rr->rdlen <= max) {
        out[p] = 0xc0;
        out[p + 1u] = 0x0c; /* pointer to offset 12 */
        p += 2u;
    } else {
        rc = encode_name(rr->name, out + p, &nlen, max - p);
        if (rc != ATN_OK) {
            return rc;
        }
        p += nlen;
    }
    if (p + 10u + rr->rdlen > max) {
        return ATN_ERR_LEN;
    }
    wr16(out + p, rr->type);
    wr16(out + p + 2, rr->rrclass);
    wr32(out + p + 4, rr->ttl);
    wr16(out + p + 8, rr->rdlen);
    p += 10u;
    if (rr->rdlen > 0) {
        memcpy(out + p, rr->rdata, rr->rdlen);
        p += rr->rdlen;
    }
    *pos = p;
    return ATN_OK;
}

int atn_dns_respond(const atn_dns_zone *z, const atn_dns_q *q,
                    const uint8_t *query, size_t qn,
                    uint8_t *out, size_t *outn, size_t max)
{
    uint16_t flags, rcode = ATN_DNS_RCODE_NOERROR, an = 0;
    size_t pos, qend;
    unsigned i;
    int aa = 0, name_exists = 0, rc;
    uint8_t tmp[ATN_DNS_MAX_MSG];
    uint8_t *dst;

    if (z == NULL || q == NULL || query == NULL || out == NULL || outn == NULL) {
        return ATN_ERR_PARAM;
    }
    if (max < 12u) {
        return ATN_ERR_LEN;
    }
    dst = (max >= sizeof(tmp)) ? tmp : out;

    if (q->qtype == 0xffffu) {
        rcode = ATN_DNS_RCODE_NOTIMP;
    } else if (q->qclass != ATN_DNS_CLASS_IN && q->qclass != 0) {
        rcode = ATN_DNS_RCODE_REFUSED;
    } else if (!in_zone(z->apex, q->qname)) {
        rcode = ATN_DNS_RCODE_REFUSED;
    } else {
        aa = 1;
        for (i = 0; i < z->n; i++) {
            if (strcmp(z->rr[i].name, q->qname) == 0) {
                name_exists = 1;
            }
        }
        if (!name_exists) {
            rcode = ATN_DNS_RCODE_NXDOMAIN;
        }
    }

    /* Copy question: from byte 12 through QNAME+QTYPE+QCLASS. */
    qend = 12;
    if (qn >= 12u) {
        size_t off = 12;
        char skip[ATN_DNS_MAX_NAME + 1u];
        if (decode_name(query, qn, &off, skip, sizeof(skip)) == ATN_OK &&
            off + 4u <= qn) {
            qend = off + 4u;
        }
    }
    if (qend > max || qend > sizeof(tmp)) {
        return ATN_ERR_LEN;
    }
    memcpy(dst, query, qend);
    wr16(dst, q->id);
    pos = qend;

    if (rcode == ATN_DNS_RCODE_NOERROR && aa) {
        for (i = 0; i < z->n; i++) {
            if (strcmp(z->rr[i].name, q->qname) == 0 &&
                z->rr[i].type == q->qtype &&
                z->rr[i].rrclass == ATN_DNS_CLASS_IN) {
                rc = write_rr(dst, &pos, (max < sizeof(tmp)) ? max : sizeof(tmp),
                              &z->rr[i], 1, q->qname);
                if (rc != ATN_OK) {
                    /* Truncate: header + question only, TC=1 */
                    pos = qend;
                    an = 0;
                    flags = (uint16_t)(0x8000u | (aa ? 0x400u : 0) |
                                       (q->rd ? 0x100u : 0) | 0x200u | rcode);
                    wr16(dst + 2, flags);
                    wr16(dst + 4, 1);
                    wr16(dst + 6, 0);
                    wr16(dst + 8, 0);
                    wr16(dst + 10, 0);
                    if (pos > max) {
                        return ATN_ERR_LEN;
                    }
                    memcpy(out, dst, pos);
                    *outn = pos;
                    return ATN_OK;
                }
                an++;
            }
        }
    }

    flags = (uint16_t)(0x8000u | (aa ? 0x400u : 0) | (q->rd ? 0x100u : 0) | rcode);
    wr16(dst + 2, flags);
    wr16(dst + 4, 1); /* QDCOUNT */
    wr16(dst + 6, an);
    wr16(dst + 8, 0);
    wr16(dst + 10, 0);
    if (pos > max) {
        return ATN_ERR_LEN;
    }
    memcpy(out, dst, pos);
    *outn = pos;
    return ATN_OK;
}

static int bind_loopback(int type, uint16_t port, uint16_t *out_port, atn_sock *out)
{
    atn_sock s;
    struct sockaddr_in sa;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    int one = 1;
    s = socket(AF_INET, type, (type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP);
    if (s == ATN_INV) {
        return ATN_ERR_SOCK;
    }
#if defined(SO_REUSEADDR)
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#endif
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    if (type == SOCK_STREAM && listen(s, 8) != 0) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    if (getsockname(s, (struct sockaddr *)&sa, &slen) != 0) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    *out_port = ntohs(sa.sin_port);
    *out = s;
    return ATN_OK;
}

int atn_dns_listen(atn_dns_srv *s, uint16_t port)
{
    atn_sock u, t;
    uint16_t pu = 0, pt = 0;
    int rc;
    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    memset(s, 0, sizeof(*s));
    s->udp = (intptr_t)ATN_INV;
    s->tcp = (intptr_t)ATN_INV;
    rc = atn_net_init();
    if (rc != ATN_OK) {
        return rc;
    }
    atn_dns_zone_init(&s->zone);
    rc = bind_loopback(SOCK_DGRAM, port, &pu, &u);
    if (rc != ATN_OK) {
        return rc;
    }
    /* Same UDP port for TCP when the OS allows it. UDP stays up if TCP
     * bind fails (observed on this Windows builder with ephemeral ports). */
    rc = bind_loopback(SOCK_STREAM, pu, &pt, &t);
    if (rc != ATN_OK) {
        /* DEC-0024: Windows often refuses TCP on the UDP ephemeral port. */
        rc = bind_loopback(SOCK_STREAM, 0, &pt, &t);
        if (rc != ATN_OK) {
            t = ATN_INV;
            pt = 0;
        }
    }
    s->udp = (intptr_t)u;
    s->tcp = (intptr_t)t;
    s->port = pu;
    s->tcp_port = pt;
    return ATN_OK;
}

uint16_t atn_dns_port(const atn_dns_srv *s)
{
    return s == NULL ? 0 : s->port;
}

uint16_t atn_dns_tcp_port(const atn_dns_srv *s)
{
    return s == NULL ? 0 : s->tcp_port;
}

void atn_dns_close(atn_dns_srv *s)
{
    if (s == NULL) {
        return;
    }
    sock_close(sock_cast(s->udp));
    sock_close(sock_cast(s->tcp));
    s->udp = (intptr_t)ATN_INV;
    s->tcp = (intptr_t)ATN_INV;
}

static int handle_msg(atn_dns_srv *s, const uint8_t *in, size_t inlen,
                      uint8_t *out, size_t *outn)
{
    atn_dns_q q;
    int rc;
    rc = atn_dns_parse_query(in, inlen, &q);
    if (rc != ATN_OK) {
        /* FORMERR: ID if possible, QR=1 RCODE=1 */
        if (inlen >= 2u && *outn >= 12u) {
            memset(out, 0, 12);
            memcpy(out, in, 2);
            wr16(out + 2, (uint16_t)(0x8000u | ATN_DNS_RCODE_FORMERR));
            *outn = 12;
            return ATN_OK;
        }
        return rc;
    }
    return atn_dns_respond(&s->zone, &q, in, inlen, out, outn, ATN_DNS_MAX_MSG);
}

int atn_dns_serve_one(atn_dns_srv *s, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    atn_sock u, t;
    int r, nfds;
    uint8_t in[ATN_DNS_MAX_MSG + 2u], out[ATN_DNS_MAX_MSG];
    size_t outn = ATN_DNS_MAX_MSG;

    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    u = sock_cast(s->udp);
    t = sock_cast(s->tcp);
    if (u == ATN_INV) {
        return ATN_ERR_STATE;
    }
    FD_ZERO(&rfds);
    FD_SET(u, &rfds);
    nfds = (int)u + 1;
    if (t != ATN_INV) {
        FD_SET(t, &rfds);
        if ((int)t + 1 > nfds) {
            nfds = (int)t + 1;
        }
    }
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
    r = select(nfds, &rfds, NULL, NULL, &tv);
    if (r == 0) {
        return ATN_ERR_STATE;
    }
    if (r < 0) {
        return ATN_ERR_SOCK;
    }
    if (FD_ISSET(u, &rfds)) {
        struct sockaddr_in sa;
        int n;
#if defined(ATN_OS_WINDOWS)
        int slen = (int)sizeof(sa);
        n = recvfrom(u, (char *)in, (int)ATN_DNS_MAX_MSG, 0,
                     (struct sockaddr *)&sa, &slen);
#else
        socklen_t slen = sizeof(sa);
        n = (int)recvfrom(u, in, ATN_DNS_MAX_MSG, 0,
                          (struct sockaddr *)&sa, &slen);
#endif
        if (n <= 0) {
            return ATN_ERR_SOCK;
        }
        s->last_peer = ntohl(sa.sin_addr.s_addr);
        s->last_peer_port = ntohs(sa.sin_port);
        outn = ATN_DNS_MAX_MSG;
        if (handle_msg(s, in, (size_t)n, out, &outn) != ATN_OK) {
            return ATN_ERR_PARAM;
        }
        memcpy(s->last_wire, out, outn);
        s->last_wire_len = outn;
#if defined(ATN_OS_WINDOWS)
        if (sendto(u, (const char *)out, (int)outn, 0,
                   (struct sockaddr *)&sa, slen) != (int)outn) {
            return ATN_ERR_SOCK;
        }
#else
        if (sendto(u, out, outn, 0, (const struct sockaddr *)&sa, slen) !=
            (ssize_t)outn) {
            return ATN_ERR_SOCK;
        }
#endif
        return ATN_OK;
    }
    if (FD_ISSET(t, &rfds)) {
        atn_sock c;
        uint8_t lenb[2];
        uint16_t ln;
        c = accept(t, NULL, NULL);
        if (c == ATN_INV) {
            return ATN_ERR_SOCK;
        }
        if (tcp_read_n(c, lenb, 2) != ATN_OK) {
            sock_close(c);
            return ATN_ERR_SOCK;
        }
        ln = rd16(lenb);
        if (ln == 0 || ln > ATN_DNS_MAX_MSG) {
            sock_close(c);
            return ATN_ERR_LEN;
        }
        if (tcp_read_n(c, in, (size_t)ln) != ATN_OK) {
            sock_close(c);
            return ATN_ERR_SOCK;
        }
        outn = ATN_DNS_MAX_MSG;
        if (handle_msg(s, in, (size_t)ln, out, &outn) != ATN_OK) {
            sock_close(c);
            return ATN_ERR_PARAM;
        }
        wr16(lenb, (uint16_t)outn);
#if defined(ATN_OS_WINDOWS)
        if (send(c, (const char *)lenb, 2, 0) != 2 ||
            send(c, (const char *)out, (int)outn, 0) != (int)outn) {
            sock_close(c);
            return ATN_ERR_SOCK;
        }
#else
        if (send(c, lenb, 2, 0) != 2 ||
            send(c, out, outn, 0) != (ssize_t)outn) {
            sock_close(c);
            return ATN_ERR_SOCK;
        }
#endif
        memcpy(s->last_wire, out, outn);
        s->last_wire_len = outn;
        sock_close(c);
        return ATN_OK;
    }
    return ATN_ERR_STATE;
}

static int build_query(uint8_t *out, size_t *n, size_t max,
                       uint16_t id, const char *qname, uint16_t qtype)
{
    size_t nlen = 0;
    int rc;
    if (max < 16u) {
        return ATN_ERR_LEN;
    }
    wr16(out, id);
    wr16(out + 2, 0x0100); /* RD=1, QUERY */
    wr16(out + 4, 1);
    wr16(out + 6, 0);
    wr16(out + 8, 0);
    wr16(out + 10, 0);
    rc = encode_name(qname, out + 12, &nlen, max - 16u);
    if (rc != ATN_OK) {
        return rc;
    }
    wr16(out + 12 + nlen, qtype);
    wr16(out + 14 + nlen, ATN_DNS_CLASS_IN);
    *n = 16u + nlen;
    return ATN_OK;
}

int atn_dns_query_udp(uint16_t port, const char *qname, uint16_t qtype,
                      uint8_t *resp, size_t *n, size_t max, int timeout_ms)
{
    atn_sock s;
    struct sockaddr_in sa;
    uint8_t q[ATN_DNS_MAX_MSG];
    size_t qn = 0;
    fd_set rfds;
    struct timeval tv;
    int r, rc;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    if (port == 0 || qname == NULL || resp == NULL || n == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_net_init();
    if (rc != ATN_OK) {
        return rc;
    }
    rc = build_query(q, &qn, sizeof(q), 0x4242, qname, qtype);
    if (rc != ATN_OK) {
        return rc;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(ATN_OS_WINDOWS)
    if (sendto(s, (const char *)q, (int)qn, 0,
               (struct sockaddr *)&sa, sizeof(sa)) != (int)qn) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
#else
    if (sendto(s, q, qn, 0, (const struct sockaddr *)&sa, sizeof(sa)) !=
        (ssize_t)qn) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
#endif
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
    r = select((int)s + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) {
        sock_close(s);
        return (r == 0) ? ATN_ERR_STATE : ATN_ERR_SOCK;
    }
#if defined(ATN_OS_WINDOWS)
    r = recvfrom(s, (char *)resp, (int)max, 0, (struct sockaddr *)&sa, &slen);
#else
    r = (int)recvfrom(s, resp, max, 0, (struct sockaddr *)&sa, &slen);
#endif
    sock_close(s);
    if (r <= 0) {
        return ATN_ERR_SOCK;
    }
    *n = (size_t)r;
    return ATN_OK;
}

int atn_dns_query_tcp(uint16_t port, const char *qname, uint16_t qtype,
                      uint8_t *resp, size_t *n, size_t max, int timeout_ms)
{
    atn_sock s;
    struct sockaddr_in sa;
    uint8_t q[ATN_DNS_MAX_MSG + 2u], lenb[2];
    size_t qn = 0;
    uint16_t ln;
    fd_set rfds;
    struct timeval tv;
    int r, rc;

    if (port == 0 || qname == NULL || resp == NULL || n == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_net_init();
    if (rc != ATN_OK) {
        return rc;
    }
    rc = build_query(q + 2, &qn, sizeof(q) - 2u, 0x4242, qname, qtype);
    if (rc != ATN_OK) {
        return rc;
    }
    wr16(q, (uint16_t)qn);
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
#if defined(ATN_OS_WINDOWS)
    if (send(s, (const char *)q, (int)(qn + 2u), 0) != (int)(qn + 2u)) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
#else
    if (send(s, q, qn + 2u, 0) != (ssize_t)(qn + 2u)) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
#endif
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
    r = select((int)s + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) {
        sock_close(s);
        return (r == 0) ? ATN_ERR_STATE : ATN_ERR_SOCK;
    }
    if (tcp_read_n(s, lenb, 2) != ATN_OK) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    ln = rd16(lenb);
    if (ln == 0 || ln > max || ln > ATN_DNS_MAX_MSG) {
        sock_close(s);
        return ATN_ERR_LEN;
    }
    if (tcp_read_n(s, resp, (size_t)ln) != ATN_OK) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    sock_close(s);
    *n = (size_t)ln;
    return ATN_OK;
}
